#include "ast.h"
#include "codegen.h"
#include "parser.h"
#include "token.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* x86-64 SysV ABI, AT&T syntax.
 *
 * resolve() first walks the tree: builds the symbol tables, resolves
 * every ND_VAR to an Obj, types every expression (nodes carry no
 * type when they come out of the parser) and folds in pointer
 * arithmetic rules. codegen() then turns the annotated tree into
 * assembly with a stack machine: every expression leaves its result
 * in %rax, binary operators spill the right operand and combine in
 * %rdi, calls push all arguments and pop the first six into regs.
 *
 * known gaps, on purpose:
 *   - floats: type system accepts them, backend doesn't
 *   - no short-circuit for &&/||, and >> is always arithmetic
 *   - no 16-byte stack alignment padding on big calls (FIXME)
 */

typedef struct Scope Scope;

struct Obj {
  Obj *next;
  char *name;
  Type *type;
  int offset;          /* rbp-relative slot for locals/params */
  int frame;           /* function: total frame size */
  int is_local;
  int is_global;
  int is_function;
};

struct Scope {
  Scope *next;
  Obj *vars;
};

static Scope base_scope;
static Scope *scope = &base_scope;

static int labeln;
static int cur_offset;
static char *cur_section = "";

static FILE *out;
static int brk_labels[64], cont_labels[64];
static int brk_n, cont_n;
static int ret_label;

/* ------------------------------------------------------------------ */
/* symbol tables and type inference (resolve)                          */
/* ------------------------------------------------------------------ */

static Obj *new_obj(char *name, Type *type) {
  Obj *o = xmalloc(sizeof(Obj));
  memset(o, 0, sizeof(Obj));
  o->name = name;
  o->type = type;
  return o;
}

static Obj *find_var(char *name) {
  for (Scope *s = scope; s; s = s->next)
    for (Obj *o = s->vars; o; o = o->next)
      if (strcmp(o->name, name) == 0)
        return o;
  return NULL;
}

static void enter_scope(void) {
  Scope *s = xmalloc(sizeof(Scope));
  memset(s, 0, sizeof(Scope));
  s->next = scope;
  scope = s;
}

static void leave_scope(void) {
  scope = scope->next;
}

static void push_var(Obj *o) {
  o->next = scope->vars;
  scope->vars = o;
}

static int roundup(int n, int m) {
  return (n + m - 1) / m * m;
}

static void check_type_supported(Type *t) {
  /* doubles use SSE; the 4-byte float type still has no backend */
  static Type *marked;   /* chain of structs being walked, so
                          * self-referential ones terminate */
  for (;;) {
    switch (t->kind) {
      case TY_FLOAT:
        error("float not implemented, use double");
        return;
      case TY_PTR:
      case TY_ARRAY:
        t = t->base;
        continue;
      case TY_STRUCT:
        for (Type *t2 = marked; t2; t2 = t2->mark_prev)
          if (t2 == t)
            return;
        t->mark_prev = marked;
        marked = t;
        for (Member *m = t->members; m; m = m->next)
          check_type_supported(m->type);
        marked = t->mark_prev;
        return;
      default:
        return;
    }
  }
}

static Member *find_member(Type *st, char *name) {
  for (Member *m = st->members; m; m = m->next)
    if (strcmp(m->name, name) == 0)
      return m;
  return NULL;
}

static Type *result_type(Type *a, Type *b) {
  Type *t = (a->kind == TY_LONG || b->kind == TY_LONG) ?
            type_new(TY_LONG) : type_new(TY_INT);
  t->is_unsigned = a->is_unsigned || b->is_unsigned;
  return t;
}

static void resolve_expr(Node *n);

/* wrap a value conversion; the backend knows int->double
 * (cvtsi2sd) and double->int (cvttsd2si) only */
static Node *cast_of(Node *n, Type *to) {
  if (n->type->kind == TY_DOUBLE && to->kind == TY_DOUBLE)
    return n;
  if (n->type->kind != TY_DOUBLE && to->kind != TY_DOUBLE)
    return n;
  Node *c = xmalloc(sizeof(Node));
  c->kind = ND_CAST;
  c->lhs = n;
  c->targ = to;
  c->type = to;
  return c;
}

static void resolve_num(Node *n) {
  n->type = n->is_float ? type_new(TY_DOUBLE) : type_new(TY_INT);
}

static void resolve_bin(Node *n) {
  resolve_expr(n->lhs);
  resolve_expr(n->rhs);

  Type *l = n->lhs->type;
  Type *r = n->rhs->type;

  /* usual arithmetic conversions: any double operand drags the
   * integer side up as a cast; % and the bitwise/shift ops have
   * no double form at all */
  int dbl = l->kind == TY_DOUBLE || r->kind == TY_DOUBLE;
  if (dbl) {
    if (n->op == '%' || n->op == '&' || n->op == '|' ||
        n->op == '^' || n->op == OP_SHL || n->op == OP_SHR)
      error("invalid operands to binary operator");
    if (l->kind != TY_DOUBLE)
      n->lhs = cast_of(n->lhs, type_new(TY_DOUBLE));
    else if (r->kind != TY_DOUBLE)
      n->rhs = cast_of(n->rhs, type_new(TY_DOUBLE));
  }

  switch (n->op) {
    case '+':
    case '-':
      if (l->kind == TY_PTR && r->kind == TY_PTR) {
        if (n->op == '+')
          error("adding two pointers");
        n->type = type_new(TY_LONG);   /* ptrdiff_t */
        return;
      }
      if (l->kind == TY_PTR) {
        n->type = l;
        return;
      }
      if (r->kind == TY_PTR) {
        if (n->op == '-')
          error("subtracting a pointer from an integer");
        n->type = r;
        return;
      }
      n->type = dbl ? type_new(TY_DOUBLE) : result_type(l, r);
      return;
    case '*':
    case '/':
      if (l->kind == TY_PTR || r->kind == TY_PTR)
        error("invalid operands to binary operator");
      n->type = dbl ? type_new(TY_DOUBLE) : result_type(l, r);
      return;
    case '%':
    case '&':
    case '|':
    case '^':
    case OP_SHL:
    case OP_SHR:
      if (l->kind == TY_PTR || r->kind == TY_PTR)
        error("invalid operands to binary operator");
      n->type = result_type(l, r);
      return;
    default:   /* comparisons are int */
      n->type = type_new(TY_INT);
      return;
  }
}

static void resolve_unary(Node *n) {
  resolve_expr(n->lhs);
  Type *ot = n->lhs->type;

  switch (n->op) {
    case '*':
      /* *fp on a pointer-to-function designates the function itself */
      if (ot->kind == TY_PTR && ot->base->kind == TY_FUNC) {
        n->type = ot->base;
        return;
      }
      if (ot->kind != TY_PTR)
        error("cannot dereference non-pointer");
      n->type = ot->base;
      return;
    case '&':
      n->type = ptr_to(ot);
      return;
    case OP_INC:
    case OP_DEC:
      n->type = ot;
      return;
    case '!':
      n->type = type_new(TY_INT);
      return;
    case '~':
      if (ot->kind == TY_DOUBLE)
        error("invalid operands to binary operator");
      n->type = ot;
      return;
    default:   /* + - keep the operand's type */
      n->type = ot;
      return;
  }
}

static void resolve_cond(Node *n) {
  resolve_expr(n->cond);
  resolve_expr(n->then);
  resolve_expr(n->els);
  if (n->then->type->kind == TY_DOUBLE &&
      n->els->type->kind != TY_DOUBLE)
    n->els = cast_of(n->els, type_new(TY_DOUBLE));
  else if (n->then->type->kind != TY_DOUBLE &&
           n->els->type->kind == TY_DOUBLE)
    n->then = cast_of(n->then, type_new(TY_DOUBLE));
  n->type = n->then->type;
}

/* the only node kind that gets a type assigned when it's built is
 * ND_DECL; everything else lands here */
static void resolve_expr(Node *n) {
  if (!n)
    return;

  switch (n->kind) {
    case ND_NUM:
      resolve_num(n);
      return;
    case ND_STR:
      n->type = array_of(type_new(TY_CHAR), n->str_len + 1);
      return;
    case ND_VAR: {
      Obj *o = find_var(n->name);
      if (!o)
        error("undefined variable '%s'", n->name);
      n->var = o;
      n->type = o->type;
      return;
    }
    case ND_MEMBER: {
      resolve_expr(n->lhs);
      Type *st;
      if (n->is_pntr) {
        if (n->lhs->type->kind != TY_PTR ||
            n->lhs->type->base->kind != TY_STRUCT)
          error("'->' on a non-struct pointer");
        st = n->lhs->type->base;
      } else {
        if (n->lhs->type->kind != TY_STRUCT)
          error("'.' on a non-struct");
        st = n->lhs->type;
      }
      Member *m = find_member(st, n->name);
      if (!m)
        error("no member named '%s'", n->name);
      check_type_supported(m->type);
      n->type = m->type;
      return;
    }
    case ND_ASSIGN: {
      resolve_expr(n->lhs);
      resolve_expr(n->rhs);
      Type *lt = n->lhs->type;
      if (lt->kind == TY_STRUCT)
        error("struct assignment not implemented");
      if (lt->kind == TY_ARRAY || lt->kind == TY_FUNC)
        error("can't assign to an array or function");
      if (n->op != '=' && lt->kind != TY_DOUBLE &&
          n->rhs->type->kind == TY_DOUBLE)
        error("unsupported compound assignment");
      if (lt->kind == TY_DOUBLE &&
          n->rhs->type->kind != TY_DOUBLE)
        n->rhs = cast_of(n->rhs, type_new(TY_DOUBLE));
      else if (lt->kind != TY_DOUBLE &&
               n->rhs->type->kind == TY_DOUBLE)
        n->rhs = cast_of(n->rhs, lt);
      if (n->op != '=' && lt->kind == TY_PTR &&
          n->op != OP_ADD_ASSIGN && n->op != OP_SUB_ASSIGN)
        error("invalid compound assignment on a pointer");
      n->type = lt;
      return;
    }
    case ND_BIN:
      resolve_bin(n);
      return;
    case ND_UNARY:
      resolve_unary(n);
      return;
    case ND_COND:
      resolve_cond(n);
      return;
    case ND_CALL: {
      Node *callee = n->lhs;

      if (callee->kind == ND_VAR && !find_var(callee->name)) {
        /* implicit function declaration, classic C style; the
         * definition, if it shows up later, will shadow this one */
        Type *ft = func_type(type_new(TY_INT));
        Obj *o = new_obj(callee->name, ft);
        o->is_function = 1;
        push_var(o);
        callee->var = o;
        callee->type = ft;
      }

      resolve_expr(callee);

      Type *ft;
      if (callee->type->kind == TY_FUNC)
        ft = callee->type;
      else if (callee->type->kind == TY_PTR &&
               callee->type->base->kind == TY_FUNC)
        ft = callee->type->base;
      else
        error("called object is not a function");

      for (Node *a = n->args; a; a = a->next)
        resolve_expr(a);
      n->type = ft->ret;
      return;
    }
    case ND_INDEX:
      resolve_expr(n->lhs);
      resolve_expr(n->rhs);
      if (n->lhs->type->kind == TY_ARRAY ||
          n->lhs->type->kind == TY_PTR)
        n->type = n->lhs->type->base;
      else
        error("subscripted value is not an array or pointer");
      return;
    case ND_SIZEOF:
      if (n->lhs)
        resolve_expr(n->lhs);
      else
        check_type_supported(n->targ);
      n->type = type_new(TY_INT);
      return;
    default:
      error("internal: unexpected node kind %d", n->kind);
  }
}

static void resolve_stmt(Node *n);

static void resolve_block(Node *n) {
  enter_scope();
  for (Node *s = n->body; s; s = s->next)
    resolve_stmt(s);
  leave_scope();
}

static Type *cur_ret;   /* return type of the function being walked */

static void resolve_stmt(Node *n) {
  switch (n->kind) {
    case ND_BLOCK:
      resolve_block(n);
      return;
    case ND_DECL: {
      check_type_supported(n->type);
      if (n->type->kind == TY_VOID)
        error("variable '%s' declared void", n->name);
      Obj *o = new_obj(n->name, n->type);
      o->is_local = 1;
      cur_offset -= roundup(o->type->size, 8);
      o->offset = cur_offset;
      push_var(o);
      n->var = o;
      if (n->init) {
        resolve_expr(n->init);
        if (n->type->kind == TY_DOUBLE &&
            n->init->type->kind != TY_DOUBLE)
          n->init = cast_of(n->init, type_new(TY_DOUBLE));
        else if (n->type->kind != TY_DOUBLE &&
                 n->init->type->kind == TY_DOUBLE)
          n->init = cast_of(n->init, n->type);
        if (n->type->kind == TY_STRUCT)
          error("struct initializers not implemented");
      }
      return;
    }
    case ND_EXPR_STMT:
      if (n->lhs)
        resolve_expr(n->lhs);
      return;
    case ND_IF:
      resolve_expr(n->cond);
      resolve_stmt(n->then);
      if (n->els)
        resolve_stmt(n->els);
      return;
    case ND_WHILE:
      resolve_expr(n->cond);
      resolve_stmt(n->then);
      return;
    case ND_DO_WHILE:
      resolve_stmt(n->then);
      resolve_expr(n->cond);
      return;
    case ND_FOR:
      /* C99: the init variables live for the whole loop */
      enter_scope();
      for (Node *s = n->init; s; s = s->next)
        resolve_stmt(s);
      if (n->cond)
        resolve_expr(n->cond);
      if (n->inc)
        resolve_expr(n->inc);
      resolve_stmt(n->then);
      leave_scope();
      return;
    case ND_RETURN:
      if (n->lhs) {
        resolve_expr(n->lhs);
        if (cur_ret->kind == TY_DOUBLE &&
            n->lhs->type->kind != TY_DOUBLE)
          n->lhs = cast_of(n->lhs, type_new(TY_DOUBLE));
        else if (cur_ret->kind != TY_DOUBLE &&
                 n->lhs->type->kind == TY_DOUBLE)
          n->lhs = cast_of(n->lhs, cur_ret);
      }
      return;
    case ND_BREAK:
    case ND_CONTINUE:
      return;
    default:
      resolve_expr(n);
      return;
  }
}

void resolve(Node *prog) {
  scope = &base_scope;
  base_scope.next = NULL;
  base_scope.vars = NULL;
  labeln = 0;

  /* pass 1: every global and function goes into the base scope up
   * front, so references work in any declaration order */
  for (Node *n = prog; n; n = n->next) {
    Obj *o;
    if (n->kind == ND_FUNC) {
      o = new_obj(n->name, n->type);
      o->is_function = 1;
    } else if (n->kind == ND_DECL) {
      o = new_obj(n->name, n->type);
      o->is_global = 1;
    } else {
      error("internal: unexpected top-level node");
      return;
    }
    if (find_var(n->name))   /* first declaration wins */
      continue;
    push_var(o);
    n->var = o;
  }

  /* pass 2: type the globals' initializers, then each function
   * body against a fresh function scope */
  for (Node *n = prog; n; n = n->next) {
    if (n->kind == ND_DECL) {
      check_type_supported(n->type);
      if (n->type->kind == TY_VOID && n->var)
        error("variable '%s' declared void", n->name);
      if (n->init)
        resolve_expr(n->init);
      continue;
    }

    Type *ft = n->type;
    check_type_supported(ft->ret);
    for (Node *p = ft->params; p; p = p->next) {
      check_type_supported(p->type);
      if (p->type->kind == TY_STRUCT)
        error("passing structs by value not implemented");
    }
    if (ft->ret->kind == TY_STRUCT)
      error("returning structs by value not implemented");

    if (!n->body)
      continue;   /* prototype only */

    enter_scope();
    cur_offset = 0;
    cur_ret = ft->ret;
    for (Node *p = ft->params; p; p = p->next) {
      Obj *po = new_obj(p->name, p->type);
      po->is_local = 1;
      /* array parameters decay to pointers, per C */
      if (po->type->kind == TY_ARRAY) {
        po->type = ptr_to(po->type->base);
        p->type = po->type;
      }
      cur_offset -= roundup(po->type->size, 8);
      po->offset = cur_offset;
      push_var(po);
      p->var = po;
    }
    resolve_block(n->body);
    leave_scope();
    n->var->frame = roundup(-cur_offset, 16);
  }
}

/* ------------------------------------------------------------------ */
/* codegen                                                            */
/* ------------------------------------------------------------------ */

static void section(char *s) {
  if (strcmp(s, cur_section) == 0)
    return;
  cur_section = s;
  fprintf(out, "  .section %s\n", s);
}

/* double constants have no immediate form; each distinct literal gets
 * a .rodata slot, registered here on first use. forward references
 * are fine, GAS resolves them within the file */
static double consts[256];
static int consts_n;

static void emit_const(double d) {
  for (int i = 0; i < consts_n; i++)
    if (consts[i] == d) {
      fprintf(out, "  movsd .LC%d(%%rip), %%xmm0\n", i);
      return;
    }
  if (consts_n == 256)
    error("too many floating point constants");
  consts[consts_n] = d;
  unsigned long long bits;
  memcpy(&bits, &d, 8);
  fprintf(out, "  movsd .LC%d(%%rip), %%xmm0\n", consts_n);
  section(".rodata");
  fprintf(out, ".LC%d:\n  .quad 0x%llx\n", consts_n, bits);
  section(".text");
  consts_n++;
}

/* load the value at (%rax) into %rax (ints) or %xmm0 (doubles),
 * sign or zero extending to fit the declared type. this is where
 * signedness enters the register */
static void load(Type *t) {
  if (t->kind == TY_DOUBLE) {
    fprintf(out, "  movsd (%%rax), %%xmm0\n");
    return;
  }
  switch (t->size) {
    case 1:
      fprintf(out, "  %s (%%rax), %%eax\n",
              t->is_unsigned ? "movzbl" : "movsbl");
      return;
    case 2:
      fprintf(out, "  %s (%%rax), %%eax\n",
              t->is_unsigned ? "movzwl" : "movswl");
      return;
    case 4:
      if (t->is_unsigned)
        fprintf(out, "  movl (%%rax), %%eax\n");
      else
        fprintf(out, "  movslq (%%rax), %%rax\n");
      return;
    default:
      fprintf(out, "  mov (%%rax), %%rax\n");
      return;
  }
}

/* store %rax (ints) or %xmm0 (doubles) at (%rdi) */
static void store(Type *t) {
  if (t->kind == TY_DOUBLE) {
    fprintf(out, "  movsd %%xmm0, (%%rdi)\n");
    return;
  }
  switch (t->size) {
    case 1:
      fprintf(out, "  mov %%al, (%%rdi)\n");
      return;
    case 2:
      fprintf(out, "  mov %%ax, (%%rdi)\n");
      return;
    case 4:
      fprintf(out, "  mov %%eax, (%%rdi)\n");
      return;
    default:
      fprintf(out, "  mov %%rax, (%%rdi)\n");
      return;
  }
}

static void emit_string(Node *n);
static void gen_expr(Node *n);
static void gen_stmt(Node *n);

/* address of an lvalue into %rax: stack slot, global symbol, index
 * arithmetic (scaled), or a dereferenced pointer value */
static void gen_addr(Node *n) {
  switch (n->kind) {
    case ND_VAR: {
      Obj *o = n->var;
      if (o->is_local)
        fprintf(out, "  lea %d(%%rbp), %%rax\n", o->offset);
      else
        fprintf(out, "  lea %s(%%rip), %%rax\n", o->name);
      return;
    }
    case ND_INDEX: {
      gen_expr(n->lhs);
      fprintf(out, "  push %%rax\n");
      gen_expr(n->rhs);
      int sz = type_size(n->lhs->type->base);
      if (sz > 1)
        fprintf(out, "  imul $%d, %%rax, %%rax\n", sz);
      fprintf(out, "  pop %%rdi\n");
      fprintf(out, "  add %%rdi, %%rax\n");
      return;
    }
    case ND_MEMBER: {
      /* any struct-typed lvalue (var, member, deref, index) already
       * evaluated to an address, since load() skips TY_STRUCT */
      gen_expr(n->lhs);
      Type *st = n->is_pntr ? n->lhs->type->base : n->lhs->type;
      Member *m = find_member(st, n->name);
      fprintf(out, "  add $%d, %%rax\n", m->offset);
      return;
    }
    case ND_UNARY:
      if (n->op == '*') {
        gen_expr(n->lhs);
        return;
      }
      error("not an lvalue");
      return;
    default:
      error("not an lvalue");
  }
}

/* combine %rax/%xmm0 (lhs) with %rdi/%xmm1 (rhs), result in
 * %rax (ints) or %xmm0 (doubles, promotions done in resolve).
 * pointers get their integer operand scaled on the way in */
static void emit_combine(int op, Type *lhs_ty, Type *rhs_ty) {
  if (lhs_ty->kind == TY_DOUBLE || rhs_ty->kind == TY_DOUBLE) {
    switch (op) {
      case '+':   fprintf(out, "  addsd %%xmm1, %%xmm0\n"); return;
      case '-':   fprintf(out, "  subsd %%xmm1, %%xmm0\n"); return;
      case '*':   fprintf(out, "  mulsd %%xmm1, %%xmm0\n"); return;
      case '/':   fprintf(out, "  divsd %%xmm1, %%xmm0\n"); return;
      case OP_LOGAND:
      case OP_LOGOR: {
        /* no short-circuiting; normalize both to 0/1 in int regs */
        fprintf(out, "  movsd %%xmm0, %%xmm3\n");
        fprintf(out, "  movsd %%xmm1, %%xmm4\n");
        emit_const(0.0);
        fprintf(out, "  movsd %%xmm0, %%xmm2\n");
        fprintf(out, "  ucomisd %%xmm2, %%xmm3\n");
        fprintf(out, "  setne %%al\n");
        fprintf(out, "  movzbq %%al, %%rax\n");
        fprintf(out, "  ucomisd %%xmm2, %%xmm4\n");
        fprintf(out, "  setne %%dl\n");
        fprintf(out, "  movzbq %%dl, %%rdx\n");
        fprintf(out, "  %s %%rdx, %%rax\n",
                op == OP_LOGAND ? "and" : "or");
        return;
      }
      default: {   /* comparisons */
        char *set;
        switch (op) {
          case '<':   set = "setb";  break;
          case '>':   set = "seta";  break;
          case OP_LE: set = "setbe"; break;
          case OP_GE: set = "setae"; break;
          case OP_EQ: set = "sete";  break;
          default:    set = "setne"; break;
        }
        /* ucomisd %xmm1, %xmm0 sets flags per (xmm0 vs xmm1):
         * a<b -> CF, a==b -> ZF. unordered NaN operands look like
         * "less than" here, same as most compilers without
         * -ffast-math ever caring */
        fprintf(out, "  ucomisd %%xmm1, %%xmm0\n");
        fprintf(out, "  %s %%al\n", set);
        fprintf(out, "  movzbq %%al, %%rax\n");
        return;
      }
    }
  }

  int size = 0;
  if (lhs_ty->kind == TY_PTR)
    size = type_size(lhs_ty->base);
  else if (rhs_ty->kind == TY_PTR)
    size = type_size(rhs_ty->base);

  switch (op) {
    case '+':
      if (lhs_ty->kind == TY_PTR)
        fprintf(out, "  imul $%d, %%rdi, %%rdi\n", size);
      else if (rhs_ty->kind == TY_PTR)
        fprintf(out, "  imul $%d, %%rax, %%rax\n", size);
      fprintf(out, "  add %%rdi, %%rax\n");
      return;
    case '-':
      if (lhs_ty->kind == TY_PTR && rhs_ty->kind == TY_PTR) {
        /* difference in bytes, then scaled back to elements */
        fprintf(out, "  sub %%rdi, %%rax\n");
        fprintf(out, "  mov $%d, %%rcx\n", size);
        fprintf(out, "  cqo\n");
        fprintf(out, "  idiv %%rcx\n");
        return;
      }
      if (lhs_ty->kind == TY_PTR)
        fprintf(out, "  imul $%d, %%rdi, %%rdi\n", size);
      fprintf(out, "  sub %%rdi, %%rax\n");
      return;
    case '*':
      fprintf(out, "  imul %%rdi, %%rax\n");
      return;
    case '/':
    case '%':
      if (lhs_ty->is_unsigned)
        fprintf(out, "  xor %%edx, %%edx\n");
      else
        fprintf(out, "  cqo\n");
      fprintf(out, "  %s %%rdi\n", lhs_ty->is_unsigned ? "div" : "idiv");
      if (op == '%')
        fprintf(out, "  mov %%rdx, %%rax\n");
      return;
    case '&':
      fprintf(out, "  and %%rdi, %%rax\n");
      return;
    case '|':
      fprintf(out, "  or %%rdi, %%rax\n");
      return;
    case '^':
      fprintf(out, "  xor %%rdi, %%rax\n");
      return;
    case OP_SHL:
    case OP_SHR:
      fprintf(out, "  mov %%rdi, %%rcx\n");
      /* FIXME: >> is always arithmetic; unsigned operands want shr */
      fprintf(out, "  %s %%cl, %%rax\n", op == OP_SHL ? "shl" : "sar");
      return;
    case OP_LOGAND:
    case OP_LOGOR: {
      /* no short-circuiting, results are normalized 0/1 */
      fprintf(out, "  cmp $0, %%rdi\n");
      fprintf(out, "  setne %%dl\n");
      fprintf(out, "  movzbq %%dl, %%rdx\n");
      fprintf(out, "  cmp $0, %%rax\n");
      fprintf(out, "  setne %%al\n");
      fprintf(out, "  movzbq %%al, %%rax\n");
      fprintf(out, "  %s %%rdx, %%rax\n", op == OP_LOGAND ? "and" : "or");
      return;
    }
    default: {   /* comparisons */
      int uns = lhs_ty->kind != TY_PTR && lhs_ty->is_unsigned;
      char *set;
      switch (op) {
        case '<':   set = uns ? "setb" : "setl";  break;
        case '>':   set = uns ? "seta" : "setg";  break;
        case OP_LE: set = uns ? "setbe" : "setle"; break;
        case OP_GE: set = uns ? "setae" : "setge"; break;
        case OP_EQ: set = "sete";  break;
        default:    set = "setne"; break;
      }
      fprintf(out, "  cmp %%rdi, %%rax\n");
      fprintf(out, "  %s %%al\n", set);
      fprintf(out, "  movzbq %%al, %%rax\n");
      return;
    }
  }
}

static void gen_call(Node *n) {
  int nargs = 0;
  for (Node *a = n->args; a; a = a->next)
    nargs++;
  Node **args = xmalloc(sizeof(Node *) * nargs);
  int i = 0;
  for (Node *a = n->args; a; a = a->next) {
    if (a->type->kind == TY_STRUCT)
      error("passing structs by value not implemented");
    args[i++] = a;
  }

  /* SysV: ints go to rdi..r9, doubles to xmm0..7, each class counted
   * independently and in argument order; anything past the budget
   * lands on the stack, still in argument order */
  static char *intregs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
  int nsse = 0;
  {
    int ireg = 0, sreg = 0;
    for (i = 0; i < nargs; i++)
      if (args[i]->type->kind == TY_DOUBLE) {
        if (sreg < 8) { sreg++; nsse++; }
      } else if (ireg < 6) {
        ireg++;
      }
  }

  /* odd argument counts would leave rsp 8 bytes off the SysV call
   * alignment; the filler goes below all the slots, so the callee
   * never sees it, its only job is the parity */
  int fill = nargs % 2;
  if (fill)
    fprintf(out, "  push $0\n");

  /* evaluate right-to-left, one 8-byte slot per argument */
  for (i = nargs - 1; i >= 0; i--) {
    gen_expr(args[i]);
    if (args[i]->type->kind == TY_DOUBLE) {
      fprintf(out, "  sub $8, %%rsp\n");
      fprintf(out, "  movsd %%xmm0, (%%rsp)\n");
    } else {
      fprintf(out, "  push %%rax\n");
    }
  }

  /* after the pushes, argument i is at i*8(%rsp); draft the register
   * arguments straight from there, preserving argument order */
  int ireg = 0, sreg = 0;
  for (i = 0; i < nargs; i++) {
    if (args[i]->type->kind == TY_DOUBLE) {
      if (sreg < 8)
        fprintf(out, "  movsd %d(%%rsp), %%xmm%d\n", i * 8, sreg++);
    } else if (ireg < 6) {
      fprintf(out, "  mov %d(%%rsp), %s\n", i * 8, intregs[ireg++]);
    }
  }

  /* compact the stack arguments down to slots 0.. so the callee
   * finds them contiguous from 16(%rbp) on; the slots they leave
   * behind are above rsp and never read again */
  int ireg2 = 0, sreg2 = 0, stk = 0;
  for (i = 0; i < nargs; i++) {
    int is_stk;
    if (args[i]->type->kind == TY_DOUBLE)
      is_stk = sreg2++ >= 8;
    else
      is_stk = ireg2++ >= 6;
    if (is_stk) {
      if (args[i]->type->kind == TY_DOUBLE) {
        fprintf(out, "  movsd %d(%%rsp), %%xmm0\n", i * 8);
        fprintf(out, "  movsd %%xmm0, %d(%%rsp)\n", stk * 8);
      } else {
        fprintf(out, "  mov %d(%%rsp), %%rax\n", i * 8);
        fprintf(out, "  mov %%rax, %d(%%rsp)\n", stk * 8);
      }
      stk++;
    }
  }

  /* SysV varargs: %al carries the vector-register arg count */
  fprintf(out, "  mov $%d, %%al\n", nsse);

  if (n->lhs->kind == ND_VAR && n->lhs->var->is_function) {
    fprintf(out, "  call %s\n", n->lhs->var->name);
  } else {
    gen_expr(n->lhs);
    fprintf(out, "  call *%%rax\n");
  }

  /* drop the argument slots (and the filler) to restore the frame */
  fprintf(out, "  add $%d, %%rsp\n", (nargs + fill) * 8);
}

/* evaluate a condition; jump to lbl when it is false */
static void gen_cond_jump_false(Node *cond, int lbl) {
  gen_expr(cond);
  if (cond->type->kind == TY_DOUBLE) {
    fprintf(out, "  movsd %%xmm0, %%xmm2\n");
    emit_const(0.0);
    fprintf(out, "  movsd %%xmm2, %%xmm1\n");
    fprintf(out, "  ucomisd %%xmm1, %%xmm0\n");
    fprintf(out, "  setne %%al\n");
    fprintf(out, "  movzbq %%al, %%rax\n");
  }
  fprintf(out, "  cmp $0, %%rax\n");
  fprintf(out, "  je .L%d\n", lbl);
}

/* evaluate a condition; jump to lbl when it is true */
static void gen_cond_jump_true(Node *cond, int lbl) {
  gen_expr(cond);
  if (cond->type->kind == TY_DOUBLE) {
    fprintf(out, "  movsd %%xmm0, %%xmm2\n");
    emit_const(0.0);
    fprintf(out, "  movsd %%xmm2, %%xmm1\n");
    fprintf(out, "  ucomisd %%xmm1, %%xmm0\n");
    fprintf(out, "  setne %%al\n");
    fprintf(out, "  movzbq %%al, %%rax\n");
  }
  fprintf(out, "  cmp $0, %%rax\n");
  fprintf(out, "  jne .L%d\n", lbl);
}

static void gen_expr(Node *n) {
  switch (n->kind) {
    case ND_NUM:
      if (n->is_float)
        emit_const(n->fval);
      else
        fprintf(out, "  mov $%d, %%rax\n", n->val);
      return;
    case ND_STR:
      emit_string(n);
      fprintf(out, "  lea %s(%%rip), %%rax\n", n->var->name);
      return;
    case ND_VAR:
      gen_addr(n);
      if (n->type->kind != TY_ARRAY && n->type->kind != TY_FUNC &&
          n->type->kind != TY_STRUCT)
        load(n->type);
      return;
    case ND_ASSIGN: {
      gen_addr(n->lhs);
      fprintf(out, "  push %%rax\n");

      if (n->op == '=') {
        gen_expr(n->rhs);
      } else {
        /* lhs op= rhs  ==  lhs = lhs op rhs; the compound op maps
         * back to its plain arithmetic twin */
        static struct { int from, to; } map[] = {
          {OP_ADD_ASSIGN, '+'}, {OP_SUB_ASSIGN, '-'}, {OP_MUL_ASSIGN, '*'},
          {OP_DIV_ASSIGN, '/'}, {OP_MOD_ASSIGN, '%'},
          {OP_SHL_ASSIGN, OP_SHL}, {OP_SHR_ASSIGN, OP_SHR},
          {OP_AND_ASSIGN, '&'}, {OP_OR_ASSIGN, '|'}, {OP_XOR_ASSIGN, '^'},
        };
        int op = '+';
        for (int m = 0; m < (int)(sizeof(map) / sizeof(map[0])); m++)
          if (map[m].from == n->op)
            op = map[m].to;
        if (n->type->kind == TY_DOUBLE) {
          /* double compound: park the rhs, then load the lhs */
          gen_expr(n->rhs);
          fprintf(out, "  sub $8, %%rsp\n");
          fprintf(out, "  movsd %%xmm0, (%%rsp)\n");
          gen_expr(n->lhs);
          fprintf(out, "  movsd (%%rsp), %%xmm1\n");
          fprintf(out, "  add $8, %%rsp\n");
          emit_combine(op, n->lhs->type, n->rhs->type);
        } else {
          gen_expr(n->lhs);
          fprintf(out, "  push %%rax\n");
          gen_expr(n->rhs);
          fprintf(out, "  push %%rax\n");
          fprintf(out, "  pop %%rdi\n");
          fprintf(out, "  pop %%rax\n");
          emit_combine(op, n->lhs->type, n->rhs->type);
        }
      }

      fprintf(out, "  pop %%rdi\n");
      store(n->lhs->type);
      return;
    }
    case ND_BIN:
      gen_expr(n->rhs);
      if (n->lhs->type->kind == TY_DOUBLE ||
          n->rhs->type->kind == TY_DOUBLE) {
        /* the rhs has to survive the lhs evaluation, which may itself
         * use the xmm registers, so park it on the stack */
        fprintf(out, "  sub $8, %%rsp\n");
        fprintf(out, "  movsd %%xmm0, (%%rsp)\n");
        gen_expr(n->lhs);
        fprintf(out, "  movsd (%%rsp), %%xmm1\n");
        fprintf(out, "  add $8, %%rsp\n");
      } else {
        fprintf(out, "  push %%rax\n");
        gen_expr(n->lhs);
        fprintf(out, "  pop %%rdi\n");
      }
      emit_combine(n->op, n->lhs->type, n->rhs->type);
      return;
    case ND_CAST:
      gen_expr(n->lhs);
      if (n->lhs->type->kind == TY_DOUBLE &&
          n->targ->kind != TY_DOUBLE)
        fprintf(out, "  cvttsd2si %%xmm0, %%rax\n");
      else if (n->lhs->type->kind != TY_DOUBLE &&
               n->targ->kind == TY_DOUBLE)
        fprintf(out, "  cvtsi2sd %%rax, %%xmm0\n");
      return;
    case ND_UNARY:
      switch (n->op) {
        case '&':
          gen_addr(n->lhs);
          return;
        case '*':
          /* the operand is a pointer; its value is the address
           * (and *fp on a function pointer is the function) */
          gen_expr(n->lhs);
          if (n->type->kind != TY_FUNC && n->type->kind != TY_STRUCT)
            load(n->type);
          return;
        case '+':
          gen_expr(n->lhs);
          return;
        case '-':
          gen_expr(n->lhs);
          if (n->type->kind == TY_DOUBLE) {
            /* 0.0 - x; the operand has to survive the constant load */
            fprintf(out, "  movsd %%xmm0, %%xmm2\n");
            emit_const(0.0);
            fprintf(out, "  movsd %%xmm2, %%xmm1\n");
            fprintf(out, "  subsd %%xmm1, %%xmm0\n");
          } else {
            fprintf(out, "  neg %%rax\n");
          }
          return;
        case '~':
          gen_expr(n->lhs);
          fprintf(out, "  not %%rax\n");
          return;
        case '!':
          gen_expr(n->lhs);
          if (n->lhs->type->kind == TY_DOUBLE) {
            fprintf(out, "  movsd %%xmm0, %%xmm2\n");
            emit_const(0.0);
            fprintf(out, "  movsd %%xmm2, %%xmm1\n");
            fprintf(out, "  ucomisd %%xmm1, %%xmm0\n");
          } else {
            fprintf(out, "  cmp $0, %%rax\n");
          }
          fprintf(out, "  sete %%al\n");
          fprintf(out, "  movzbq %%al, %%rax\n");
          return;
        case OP_INC:
        case OP_DEC: {
          /* pointers step by their element size. the old value has
           * to survive the mutation when the result is the old one */
          int scale = n->type->kind == TY_PTR ?
                      type_size(n->type->base) : 1;
          gen_addr(n->lhs);
          fprintf(out, "  push %%rax\n");          /* [addr] */
          load(n->type);
          if (n->type->kind == TY_DOUBLE) {
            /* doubles go through xmm0; the old value is kept in
             * xmm1 for the postfix result, and pointers step a
             * scaled constant */
            fprintf(out, "  movsd %%xmm0, %%xmm1\n");
            emit_const(1.0 * scale);
            fprintf(out, "  movsd %%xmm1, %%xmm2\n");
            fprintf(out, "  %ssd %%xmm2, %%xmm0\n",
                    n->op == OP_INC ? "add" : "sub");
            fprintf(out, "  pop %%rdi\n");         /* addr */
            fprintf(out, "  movsd %%xmm0, (%%rdi)\n");
            if (!n->is_prefix)
              fprintf(out, "  movsd %%xmm1, %%xmm0\n");
            return;
          }
          if (n->is_prefix) {
            fprintf(out, "  %s $%d, %%rax\n",
                    n->op == OP_INC ? "add" : "sub", scale);
            fprintf(out, "  pop %%rdi\n");          /* addr */
            store(n->type);
          } else {
            fprintf(out, "  push %%rax\n");         /* [addr, old] */
            fprintf(out, "  %s $%d, %%rax\n",
                    n->op == OP_INC ? "add" : "sub", scale);
            fprintf(out, "  pop %%rcx\n");          /* old value */
            fprintf(out, "  pop %%rdi\n");          /* addr */
            store(n->type);
            fprintf(out, "  mov %%rcx, %%rax\n");   /* result: old */
          }
          return;
        }
        default:
          error("internal: bad unary op %d", n->op);
      }
      return;
    case ND_COND: {
      int l1 = labeln++;
      int l2 = labeln++;
      gen_cond_jump_false(n->cond, l1);
      gen_expr(n->then);
      fprintf(out, "  jmp .L%d\n", l2);
      fprintf(out, ".L%d:\n", l1);
      gen_expr(n->els);
      fprintf(out, ".L%d:\n", l2);
      return;
    }
    case ND_CALL:
      gen_call(n);
      return;
    case ND_INDEX:
      gen_addr(n);
      if (n->type->kind != TY_ARRAY && n->type->kind != TY_FUNC &&
          n->type->kind != TY_STRUCT)
        load(n->type);
      return;
    case ND_MEMBER:
      gen_addr(n);
      if (n->type->kind != TY_ARRAY && n->type->kind != TY_FUNC &&
          n->type->kind != TY_STRUCT)
        load(n->type);
      return;
    case ND_SIZEOF:
      if (n->lhs)
        fprintf(out, "  mov $%d, %%rax\n", type_size(n->lhs->type));
      else
        fprintf(out, "  mov $%d, %%rax\n", type_size(n->targ));
      return;
    default:
      error("internal: bad expression node %d", n->kind);
  }
}

static void gen_stmt(Node *n) {
  switch (n->kind) {
    case ND_BLOCK:
      for (Node *s = n->body; s; s = s->next)
        gen_stmt(s);
      return;
    case ND_DECL:
      /* C says uninitialized locals are garbage, so only the init
       * produces code */
      if (n->init) {
        if (n->init->kind == ND_STR && n->type->kind == TY_ARRAY)
          error("array initializers not implemented");
        gen_expr(n->init);
        fprintf(out, "  push %%rax\n");
        fprintf(out, "  lea %d(%%rbp), %%rdi\n", n->var->offset);
        fprintf(out, "  pop %%rax\n");
        store(n->type);
      }
      return;
    case ND_EXPR_STMT:
      if (n->lhs)
        gen_expr(n->lhs);
      return;
    case ND_IF: {
      int l1 = labeln++;
      int l2 = labeln++;
      gen_cond_jump_false(n->cond, l1);
      gen_stmt(n->then);
      fprintf(out, "  jmp .L%d\n", l2);
      fprintf(out, ".L%d:\n", l1);
      if (n->els)
        gen_stmt(n->els);
      fprintf(out, ".L%d:\n", l2);
      return;
    }
    case ND_WHILE: {
      int l1 = labeln++;
      int l2 = labeln++;
      brk_labels[brk_n++] = l2;
      cont_labels[cont_n++] = l1;
      fprintf(out, ".L%d:\n", l1);
      gen_cond_jump_false(n->cond, l2);
      gen_stmt(n->then);
      fprintf(out, "  jmp .L%d\n", l1);
      fprintf(out, ".L%d:\n", l2);
      brk_n--;
      cont_n--;
      return;
    }
    case ND_DO_WHILE: {
      int l1 = labeln++;
      int l2 = labeln++;
      int l3 = labeln++;
      brk_labels[brk_n++] = l3;
      cont_labels[cont_n++] = l2;
      fprintf(out, ".L%d:\n", l1);
      gen_stmt(n->then);
      fprintf(out, ".L%d:\n", l2);
      gen_cond_jump_true(n->cond, l1);
      fprintf(out, ".L%d:\n", l3);
      brk_n--;
      cont_n--;
      return;
    }
    case ND_FOR: {
      int l1 = labeln++;   /* the increment, continue jumps here */
      int l2 = labeln++;   /* the condition */
      int l3 = labeln++;   /* the exit, break jumps here */
      for (Node *s = n->init; s; s = s->next)
        gen_stmt(s);
      fprintf(out, ".L%d:\n", l2);
      if (n->cond)
        gen_cond_jump_false(n->cond, l3);
      brk_labels[brk_n++] = l3;
      cont_labels[cont_n++] = l1;
      gen_stmt(n->then);
      fprintf(out, ".L%d:\n", l1);
      if (n->inc)
        gen_expr(n->inc);
      fprintf(out, "  jmp .L%d\n", l2);
      fprintf(out, ".L%d:\n", l3);
      brk_n--;
      cont_n--;
      return;
    }
    case ND_RETURN:
      if (n->lhs)
        gen_expr(n->lhs);
      fprintf(out, "  jmp .L.ret%d\n", ret_label);
      return;
    case ND_BREAK:
      fprintf(out, "  jmp .L%d\n", brk_labels[brk_n - 1]);
      return;
    case ND_CONTINUE:
      fprintf(out, "  jmp .L%d\n", cont_labels[cont_n - 1]);
      return;
    default:
      gen_expr(n);
      return;
  }
}

/* string literals land in .rodata; n->var caches the symbol so the
 * bytes are emitted at most once per literal */
static void emit_string(Node *n) {
  if (n->var)
    return;
  Obj *o = new_obj(NULL, NULL);
  o->name = xmalloc(32);
  snprintf(o->name, 32, ".L.str%d", labeln++);
  n->var = o;

  section(".rodata");
  fprintf(out, "%s:\n", o->name);
  for (int i = 0; i < n->str_len; i++)
    fprintf(out, "  .byte %u\n", (unsigned char)n->str[i]);
  fprintf(out, "  .byte 0\n");
  section(".text");
}

/* constant folding for global initializers; only expressions that
 * are integers at compile time survive this far */
static int const_fold(Node *n) {
  switch (n->kind) {
    case ND_NUM:
      if (n->is_float)
        error("floating point global initializers not implemented");
      return n->val;
    case ND_SIZEOF:
      if (n->lhs)
        return type_size(n->lhs->type);
      return type_size(n->targ);
    case ND_UNARY:
      switch (n->op) {
        case '+': return const_fold(n->lhs);
        case '-': return -const_fold(n->lhs);
        case '~': return ~const_fold(n->lhs);
        case '!': return !const_fold(n->lhs);
        default: error("unsupported global initializer");
      }
    case ND_BIN: {
      int l = const_fold(n->lhs);
      int r = const_fold(n->rhs);
      switch (n->op) {
        case '+': return l + r;
        case '-': return l - r;
        case '*': return l * r;
        case '/':
          if (r == 0)
            error("division by zero in constant expression");
          return l / r;
        case '%':
          if (r == 0)
            error("division by zero in constant expression");
          return l % r;
        case '&':  return l & r;
        case '|':  return l | r;
        case '^':  return l ^ r;
        case OP_SHL: return l << r;
        case OP_SHR: return l >> r;
        case OP_EQ:  return l == r;
        case OP_NE:  return l != r;
        case '<':  return l < r;
        case '>':  return l > r;
        case OP_LE: return l <= r;
        case OP_GE: return l >= r;
        case OP_LOGAND: return l && r;
        case OP_LOGOR:  return l || r;
        default: error("unsupported global initializer");
      }
    }
    case ND_COND:
      return const_fold(n->cond) ? const_fold(n->then) :
                                  const_fold(n->els);
    default:
      error("unsupported global initializer");
  }
  error("unsupported global initializer");
}

static void gen_data(Node *n) {
  if (n->init->kind == ND_STR) {
    /* the label must exist before the .quad references it */
    emit_string(n->init);
    section(".data");
    fprintf(out, "  .globl %s\n", n->name);
    fprintf(out, "%s:\n", n->name);
    fprintf(out, "  .quad %s\n", n->init->var->name);
    return;
  }
  section(".data");
  fprintf(out, "  .globl %s\n", n->name);
  fprintf(out, "%s:\n", n->name);
  fprintf(out, "  .%s %d\n",
          n->type->size == 1 ? "byte" :
          n->type->size == 2 ? "short" :
          n->type->size == 4 ? "long" : "quad",
          const_fold(n->init));
}

static void gen_func(Node *n) {
  section(".text");
  fprintf(out, "  .globl %s\n", n->name);
  fprintf(out, "%s:\n", n->name);
  fprintf(out, "  push %%rbp\n");
  fprintf(out, "  mov %%rsp, %%rbp\n");

  int frame = n->var->frame;
  if (frame > 0)
    fprintf(out, "  sub $%d, %%rsp\n", frame);

  /* spill the SysV registers into the param slots. ints arrive in
   * rdi..r9, doubles in xmm0..7, each class counted independently;
   * whatever overflows its budget sits on the stack in argument
   * order, at 16(%rbp) and up */
  static char *argreg[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
  int idx = 0, sse = 0, stk = 0;
  for (Node *p = n->type->params; p; p = p->next) {
    if (p->var->type->kind == TY_DOUBLE) {
      if (sse < 8)
        fprintf(out, "  movsd %%xmm%d, %d(%%rbp)\n", sse,
                p->var->offset);
      else
        fprintf(out, "  movsd %d(%%rbp), %%xmm0\n"
                     "  movsd %%xmm0, %d(%%rbp)\n",
                16 + stk * 8, p->var->offset);
      sse++;
    } else {
      if (idx < 6)
        fprintf(out, "  mov %s, %d(%%rbp)\n", argreg[idx],
                p->var->offset);
      else
        fprintf(out, "  mov %d(%%rbp), %%rax\n"
                     "  mov %%rax, %d(%%rbp)\n",
                16 + stk * 8, p->var->offset);
      idx++;
    }
    if (idx > 6 || sse > 8)
      stk++;
  }

  brk_n = 0;
  cont_n = 0;
  ret_label = labeln++;
  gen_stmt(n->body);

  fprintf(out, ".L.ret%d:\n", ret_label);
  fprintf(out, "  mov %%rbp, %%rsp\n");
  fprintf(out, "  pop %%rbp\n");
  fprintf(out, "  ret\n");
}

void codegen(Node *prog, char *outpath) {
  out = fopen(outpath, "w");
  if (!out)
    error("cannot open '%s'", outpath);

  labeln = 0;
  brk_n = 0;
  cont_n = 0;
  cur_section = "";

  for (Node *n = prog; n; n = n->next) {
    if (n->kind == ND_FUNC) {
      if (n->body)
        gen_func(n);
    } else if (n->kind == ND_DECL) {
      if (n->init)
        gen_data(n);
      else if (n->type->kind != TY_ARRAY || type_size(n->type) > 0) {
        section(".bss");
        fprintf(out, "  .globl %s\n", n->name);
        fprintf(out, "%s:\n", n->name);
        fprintf(out, "  .zero %d\n", type_size(n->type));
      }
    }
  }

  fclose(out);
}