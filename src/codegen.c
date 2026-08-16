#include "ast.h"
#include "codegen.h"
#include "parser.h"
#include "token.h"
#include "util.h"

#include "libc.h"

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
  char *name;          /* source identifier, used for scope lookup */
  char *symname;       /* emitted symbol, NULL unless mangled */
  Type *type;
  int offset;          /* rbp-relative slot for locals/params */
  int size_off;        /* VLA: rbp-relative slot holding the size the
                          declaration captured; 0 for everything else */
  int align;           /* _Alignas on the object; 0 = natural (type) */
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
static int cur_va_off;      /* the current function's ~va save area */

static FILE *out;
static int brk_labels[64], cont_labels[64];
static int brk_n, cont_n;
static int ret_label;
static Type *cur_fn_ret;   /* return type of the function being emitted */

/* function-local statics collected during resolve; codegen emits them
 * like globals after the text section */
static Node **static_decls;
static int static_decls_n, static_decls_cap;

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

/* snap a (negative, growing-down) frame offset down to the next
 * multiple of a, so an over-aligned object keeps its required
 * alignment even when the previous reservation only aligned to 8 */
static void align_snap(int *off, int a) {
  int r = *off % a;         /* negative in C for a negative *off */
  if (r)
    *off -= (a + r) % a;
}

static void check_type_supported(Type *t) {
  /* guard against infinite recursion on self-referential structs;
   * members are handled via the mark chain, so self-referential
   * ones terminate */
  static Type *marked;   /* chain of structs being walked */
  for (;;) {
    switch (t->kind) {
      case TY_PTR:
      case TY_ARRAY:
        t = t->base;
        continue;
      case TY_STRUCT:
      case TY_UNION:
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
    if (m->name && strcmp(m->name, name) == 0)
      return m;
  return NULL;
}

static Type *result_type(Type *a, Type *b) {
  Type *t = (a->kind == TY_LONG || b->kind == TY_LONG) ?
            type_new(TY_LONG) : type_new(TY_INT);
  t->is_unsigned = a->is_unsigned || b->is_unsigned;
  return t;
}

static int is_real(Type *t) {
  return t->kind == TY_FLOAT || t->kind == TY_DOUBLE;
}

/* the common real type of two operands: any double operand wins,
 * otherwise everything stays float (C's usual arithmetic
 * conversions) */
static Type *real_type(Type *a, Type *b) {
  return (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE) ?
         type_new(TY_DOUBLE) : type_new(TY_FLOAT);
}

static void resolve_expr(Node *n);
static void resolve_stmt(Node *n);
static void resolve_initializer(Node *n);
static void gen_init_stores(Node *n);

/* wrap a value conversion; the backend knows int<->double,
 * int<->float and float<->double only */
static Node *cast_of(Node *n, Type *to) {
  if (n->type->kind == to->kind)
    return n;
  if (!is_real(n->type) && !is_real(to))
    return n;
  Node *c = xmalloc(sizeof(Node));
  c->kind = ND_CAST;
  c->lhs = n;
  c->targ = to;
  c->type = to;
  return c;
}

static void resolve_num(Node *n) {
  if (n->is_float)
    n->type = n->is_f ? type_new(TY_FLOAT) : type_new(TY_DOUBLE);
  else if (n->is_long) {
    n->type = type_new(TY_LONG);
    n->type->is_unsigned = n->is_unsigned;
  } else if (n->is_unsigned) {
    n->type = type_new(TY_INT);
    n->type->is_unsigned = 1;
  } else
    n->type = type_new(TY_INT);
}

static void resolve_bin(Node *n) {
  resolve_expr(n->lhs);
  resolve_expr(n->rhs);

  if (n->op == ',') {
    /* both sides evaluated in order; the value is the right one */
    n->type = n->rhs->type;
    return;
  }

  Type *l = n->lhs->type;
  Type *r = n->rhs->type;

  if (l->kind == TY_STRUCT || r->kind == TY_STRUCT ||
      l->kind == TY_UNION || r->kind == TY_UNION)
    error("invalid operands to binary operator");

  /* usual arithmetic conversions: any floating operand drags the
   * integer side up as a cast to the wider float type; % and the
   * bitwise/shift ops have no floating form at all */
  int fp = is_real(l) || is_real(r);
  if (fp) {
    if (n->op == '%' || n->op == '&' || n->op == '|' ||
        n->op == '^' || n->op == OP_SHL || n->op == OP_SHR)
      error("invalid operands to binary operator");
    Type *real = real_type(l, r);
    if (l->kind != real->kind)
      n->lhs = cast_of(n->lhs, real);
    if (r->kind != real->kind)
      n->rhs = cast_of(n->rhs, real);
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
      n->type = fp ? real_type(l, r) : result_type(l, r);
      return;
    case '*':
    case '/':
      if (l->kind == TY_PTR || r->kind == TY_PTR)
        error("invalid operands to binary operator");
      n->type = fp ? real_type(l, r) : result_type(l, r);
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

/* an lvalue whose type is const-qualified, standing in for "can't
 * write here": a member of a const struct is const even though its
 * own type isn't */
static int lvalue_is_const(Node *n) {
  if (n->type->is_const)
    return 1;
  if (n->kind == ND_MEMBER)
    return lvalue_is_const(n->lhs);
  return 0;
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
      if (n->lhs->kind == ND_MEMBER && n->lhs->is_bitfield)
        error("cannot take the address of a bit-field");
      n->type = ptr_to(ot);
      return;
    case OP_INC:
    case OP_DEC:
      if (lvalue_is_const(n->lhs))
        error("assignment to const-qualified object");
      if (ot->kind == TY_STRUCT || ot->kind == TY_UNION)
        error("invalid operands to binary operator");
      n->type = ot;
      return;
    case '!':
      n->type = type_new(TY_INT);
      return;
    case '~':
      if (is_real(ot) || ot->kind == TY_STRUCT || ot->kind == TY_UNION)
        error("invalid operands to binary operator");
      n->type = ot;
      return;
    default:   /* + - keep the operand's type */
      if (ot->kind == TY_STRUCT || ot->kind == TY_UNION)
        error("invalid operands to binary operator");
      n->type = ot;
      return;
  }
}

static void resolve_cond(Node *n) {
  resolve_expr(n->cond);
  resolve_expr(n->then);
  resolve_expr(n->els);
  /* the branches join at the wider floating type */
  if (is_real(n->then->type) || is_real(n->els->type)) {
    Type *real = real_type(n->then->type, n->els->type);
    if (n->then->type->kind != real->kind)
      n->then = cast_of(n->then, real);
    if (n->els->type->kind != real->kind)
      n->els = cast_of(n->els, real);
  }
  n->type = n->then->type;
}

static int char_kinds_agree(Type *a, Type *b) {
  return a->is_bool == b->is_bool && a->is_unsigned == b->is_unsigned;
}

/* _Generic matches the controlling expression's type, after the
 * lvalue, array-to-pointer and function-to-pointer conversions. the
 * control side's top-level qualifiers were dropped by conversion, so
 * only its root is compared unqualified; everything nested (a pointer
 * pointee, an array element) keeps its qualifiers */
int generic_match(Type *a, Type *b, int root) {
  if (a->kind != b->kind)
    return 0;
  if ((root ? (b->is_const || b->is_volatile)
            : (a->is_const != b->is_const ||
               a->is_volatile != b->is_volatile)))
    return 0;
  switch (a->kind) {
    case TY_CHAR:
      return char_kinds_agree(a, b);
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
      return a->is_unsigned == b->is_unsigned &&
             a->is_longlong == b->is_longlong;
    case TY_FLOAT:
    case TY_DOUBLE:
      return 1;
    case TY_PTR:
      return generic_match(a->base, b->base, 0);
    case TY_ARRAY:
      return a->array_len == b->array_len &&
             generic_match(a->base, b->base, 0);
    case TY_STRUCT:
    case TY_UNION:
      return a == b;
    default:
      return 1;
  }
}

/* __builtin_types_compatible_p(T1, T2): two type names compared
 * with no decay, top-level qualifiers ignored on both sides, nested
 * ones (pointees, array elements) compared exactly, and an
 * incomplete-array bound compatible with any other bound, which is
 * how gcc answers the query (int[] vs int[5] is 1, int[3] vs int[5]
 * is 0). struct/union types match only by tag identity */
int types_compatible(Type *a, Type *b) {
  if (a->kind != b->kind)
    return 0;
  switch (a->kind) {
    case TY_CHAR:
      return char_kinds_agree(a, b);
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
      return a->is_unsigned == b->is_unsigned &&
             a->is_longlong == b->is_longlong;
    case TY_FLOAT:
    case TY_DOUBLE:
      return 1;
    case TY_PTR:
      if (a->base->is_const != b->base->is_const ||
          a->base->is_volatile != b->base->is_volatile)
        return 0;
      return types_compatible(a->base, b->base);
    case TY_ARRAY:
      if (a->array_len != b->array_len &&
          a->array_len != 0 && b->array_len != 0)
        return 0;
      if (a->base->is_const != b->base->is_const ||
          a->base->is_volatile != b->base->is_volatile)
        return 0;
      return types_compatible(a->base, b->base);
    case TY_STRUCT:
    case TY_UNION:
      return a == b;
    default:
      return 1;
  }
}

static void resolve_generic(Node *n) {
  /* the controlling expression is only examined for its type */
  resolve_expr(n->cond);
  Type *control = n->cond->type;
  if (control->kind == TY_ARRAY || control->kind == TY_FUNC)
    control = ptr_to(control->base);

  Node *chosen = NULL, *deflt = NULL;
  for (Node *a = n->els; a; a = a->next) {
    if (!a->targ) {
      if (deflt)
        error("duplicate default in _Generic");
      deflt = a->lhs;
      continue;
    }
    if (generic_match(control, a->targ, 1)) {
      if (chosen)
        error("duplicate match in _Generic");
      chosen = a->lhs;
    }
  }
  if (!chosen)
    chosen = deflt;
  if (!chosen)
    error("no association matches the _Generic controlling type");
  resolve_expr(chosen);
  n->then = chosen;
  n->type = chosen->type;
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
            (n->lhs->type->base->kind != TY_STRUCT &&
             n->lhs->type->base->kind != TY_UNION))
          error("'->' on a non-struct pointer");
        st = n->lhs->type->base;
      } else {
        if (n->lhs->type->kind != TY_STRUCT &&
            n->lhs->type->kind != TY_UNION)
          error("'.' on a non-struct");
        st = n->lhs->type;
      }
      Member *m = find_member(st, n->name);
      if (!m)
        error("no member named '%s'", n->name);
      check_type_supported(m->type);
      n->type = m->type;
      n->is_bitfield = m->is_bitfield;
      n->bit_offset = m->bit_offset;
      n->bit_width = m->bit_width;
      return;
    }
    case ND_ASSIGN: {
      resolve_expr(n->lhs);
      resolve_expr(n->rhs);
      Type *lt = n->lhs->type;
      if (lvalue_is_const(n->lhs))
        error("assignment to const-qualified object");
      if (n->op != '=' && (lt->kind == TY_STRUCT || lt->kind == TY_UNION))
        error("invalid compound assignment on a struct");
      if (lt->kind == TY_ARRAY || lt->kind == TY_FUNC)
        error("can't assign to an array or function");
      if (n->op != '=' && !is_real(lt) && is_real(n->rhs->type))
        error("unsupported compound assignment");
      if (is_real(lt) && lt->kind != n->rhs->type->kind)
        n->rhs = cast_of(n->rhs, lt);
      else if (!is_real(lt) && is_real(n->rhs->type))
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
    case ND_STMT_EXPR: {
      /* the block runs in its own scope; its value, if any, is the
       * last expression statement (void when the block ends on a
       * declaration) */
      enter_scope();
      Node *value = NULL;
      for (Node *s = n->body; s; s = s->next) {
        resolve_stmt(s);
        if (!s->next && s->kind == ND_EXPR_STMT && s->lhs)
          value = s->lhs;
      }
      leave_scope();
      if (value)
        n->type = value->type;
      else
        n->type = type_new(TY_VOID);
      return;
    }
    case ND_COND:
      resolve_cond(n);
      return;
    case ND_CAST:
      resolve_expr(n->lhs);
      if (n->lhs->type->kind == TY_STRUCT || n->targ->kind == TY_STRUCT ||
        n->lhs->type->kind == TY_UNION || n->targ->kind == TY_UNION)
        error("invalid cast on a struct");
      check_type_supported(n->targ);
      n->type = n->targ;
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
      n->var = NULL;
      if ((n->type->kind == TY_STRUCT || n->type->kind == TY_UNION) &&
          scope != &base_scope) {
        /* struct-returning call: hidden buffer in this frame for the
         * callee to write into; the call expression's value is the
         * buffer's address. global scope never reaches codegen */
        Obj *o = new_obj("~ret", n->type);
        o->is_local = 1;
        int a = type_align(o->type) > 8 ? type_align(o->type) : 8;
        cur_offset -= roundup(o->type->size, a);
        align_snap(&cur_offset, a);
        o->offset = cur_offset;
        n->var = o;
      }
      return;
    }
    case ND_INDEX:
      resolve_expr(n->lhs);
      resolve_expr(n->rhs);
      if (n->lhs->type->kind == TY_ARRAY ||
          n->lhs->type->kind == TY_PTR) {
        n->type = n->lhs->type->base;
        /* a[i] steps by a runtime stride when the element type is
         * itself a variable-length array (int a[n][m]) */
        if (type_is_vla(n->lhs->type->base)) {
          n->vla_sz = vla_size_expr(n->lhs->type->base);
          resolve_expr(n->vla_sz);
        }
      } else
        error("subscripted value is not an array or pointer");
      return;
    case ND_SIZEOF:
      if (n->lhs) {
        resolve_expr(n->lhs);
        /* sizeof(vla) is a runtime value: fold the type's dimension
         * expressions into a size tree and resolve them too; every
         * one of those variables was declared before this point */
        if (type_is_vla(n->lhs->type))
          n->vla_sz = vla_size_expr(n->lhs->type);
      } else {
        check_type_supported(n->targ);
      }
      if (n->vla_sz)
        resolve_expr(n->vla_sz);
      n->type = type_new(TY_INT);
      return;
    case ND_ALIGNOF:
      if (n->lhs) {
        resolve_expr(n->lhs);
        if (n->lhs->kind == ND_VAR && n->lhs->var && n->lhs->var->align)
          n->val = n->lhs->var->align;
        else
          n->val = type_align(n->lhs->type);
      } else {
        check_type_supported(n->targ);
        n->val = type_align(n->targ);
      }
      n->type = type_new(TY_INT);
      return;
    case ND_GENERIC:
      resolve_generic(n);
      return;
    case ND_VA_START:
      resolve_expr(n->lhs);
      n->type = type_new(TY_INT);
      n->va[3] = cur_va_off;
      return;
    case ND_VA_ARG:
      resolve_expr(n->lhs);
      n->type = n->targ;
      return;
    case ND_COMP_LIT:
      /* C99 compound literal: a hidden block-scope object with the
       * brace list as its initializer. lvalue semantics, so the
       * expression's value is the object's address */
      if (scope == &base_scope)
        error("compound literal at global scope is not supported");
      if (type_is_vla(n->targ))
        error("compound literal of a variable-length array, unsupported");
      n->type = n->targ;
      n->init = n->elems;
      resolve_initializer(n);
      {
        Obj *o = new_obj("~lit", n->type);
        o->is_local = 1;
        int a = type_align(o->type) > 8 ? type_align(o->type) : 8;
        cur_offset -= roundup(o->type->size, a);
        align_snap(&cur_offset, a);
        o->offset = cur_offset;
        push_var(o);
        n->var = o;
      }
      n->type = n->var->type;
      return;
    case ND_INIT_LIST:
      error("initializer list not allowed in an expression");
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
static Obj *cur_sret;   /* hidden "~ret" param Obj, for struct returns */

/* -------- brace initializers -------- */

/* flattening {1,2,...} into a list of (offset, type, expr) leaves. a
 * braced element feeds the aggregate's members one list element each;
 * an unbraced element "auto-braces" into the current sub-aggregate
 * and keeps consuming elements from the same list, per C's rules */

static Init *init_leafs;
static int init_leaf_n;
static int init_counting;   /* flex-array length probe: no appends */

static int is_agg(Type *t) {
  return t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION;
}

static void init_add(Type *ty, int off, Node *expr) {
  if (init_counting)
    return;
  init_leafs = xrealloc(init_leafs, sizeof(Init) * (init_leaf_n + 1));
  init_leafs[init_leaf_n].ty = ty;
  init_leafs[init_leaf_n].offset = off;
  init_leafs[init_leaf_n].expr = expr;
  init_leaf_n++;
}

/* scalar slots one element of an aggregate consumes; a zero-length
 * (flexible) nested array makes it 0, which callers reject */
static int init_scalars(Type *ty) {
  if (ty->kind == TY_ARRAY)
    return ty->array_len * init_scalars(ty->base);
  if (ty->kind == TY_STRUCT) {
    int c = 0;
    for (Member *m = ty->members; m; m = m->next)
      c += init_scalars(m->type);
    return c;
  }
  if (ty->kind == TY_UNION)
    return 1;   /* one list element covers the whole union */
  return 1;
}

static Node *init_fill(Type *ty, int off, Node *es);

/* a char leaf for string expansion */
static Node *char_node(char c) {
  Node *n = xmalloc(sizeof(Node));
  memset(n, 0, sizeof(Node));
  n->kind = ND_NUM;
  n->val = c;
  return n;
}

/* fill the members of the aggregate ty at off from the element list
 * es; the list may end early (the rest of the aggregate zero-fills).
 * returns the first unconsumed element, or NULL. leftovers are an
 * error for a braced list but feed the next sibling slot when the
 * elements auto-brace, so the caller decides */
static Node *init_fill_members(Type *ty, int off, Node *es) {
  if (ty->kind == TY_ARRAY) {
    int sz = type_size(ty->base);
    if (es && es->kind == ND_STR && ty->base->kind == TY_CHAR) {
      /* a string literal initializes a char array in one go, NUL and
       * all; shorter strings leave the rest of the array zero. a
       * string against any other element type falls through to the
       * loop and is rejected one level down */
      if (es->str_len + 1 > ty->array_len)
        error("string initializer too long");
      for (int i = 0; i < ty->array_len; i++) {
        Node *e = NULL;
        if (i < es->str_len)
          e = char_node(es->str[i]);
        else if (i == es->str_len)
          e = char_node(0);
        init_add(ty->base, off + i, e);
      }
      return es->next;
    }
    for (int i = 0; i < ty->array_len; i++) {
      if (!es) {
        /* ran out: the rest of this aggregate zero-fills */
        for (int j = i; j < ty->array_len; j++) {
          if (is_agg(ty->base))
            init_fill_members(ty->base, off + j * sz, NULL);
          else
            init_add(ty->base, off + j * sz, NULL);
        }
        return NULL;
      }
      if (es->kind == ND_DESIG) {
        /* a designator jumps the sequence to its target slot; the
         * slots before it stay zero */
        if (!es->lhs)
          error("member designator on an array");
        if (!is_const_expr(es->lhs))
          error("array designator is not a constant expression");
        CVal cv = const_fold(es->lhs);
        if (cv.is_float)
          error("array designator is not an integer constant expression");
        int idx = cv.val;
        if (idx < 0 || idx >= ty->array_len)
          error("array designator [%d] out of range for [%d]", idx,
                ty->array_len);
        if (idx > i) {
          for (; i < idx; i++) {
            if (is_agg(ty->base))
              init_fill_members(ty->base, off + i * sz, NULL);
            else
              init_add(ty->base, off + i * sz, NULL);
          }
        } else if (idx < i) {
          /* a backward designator re-fills a past slot and restarts
           * the sequence at idx + 1 (the for-increment below) */
          i = idx;
        }
        init_fill(ty->base, off + idx * sz, es->then);
        es = es->next;
        continue;
      }
      es = init_fill(ty->base, off + i * sz, es);
    }
    return es;
  }
  /* a union consumes list elements for its first member only; the
   * rest of the object zero-fills */
  for (Member *m = ty->members; m;
       m = (ty->kind == TY_UNION) ? NULL : m->next) {
    if (!es) {
      for (; m; m = m->next) {
        if (is_agg(m->type))
          init_fill_members(m->type, off + m->offset, NULL);
        else
          init_add(m->type, off + m->offset, NULL);
      }
      return NULL;
    }
    if (es->kind == ND_DESIG) {
      /* a designator jumps the sequence to its target member; the
       * members before it stay zero */
      if (es->lhs)
        error("array designator on a struct or union");
      Member *dm = find_member(ty, es->name);
      if (!dm)
        error("no member named '%s'", es->name);
      if (dm->is_bitfield)
        error("a bit-field cannot be initialized by a brace list");
      Member *p = m;
      for (; p && p != dm; p = p->next)
        ;
      if (!p) {
        /* the target member was already filled: override it in place */
        init_fill(dm->type, off + dm->offset, es->then);
        es = es->next;
        continue;
      }
      for (; m != dm; m = m->next) {
        if (is_agg(m->type))
          init_fill_members(m->type, off + m->offset, NULL);
        else
          init_add(m->type, off + m->offset, NULL);
      }
      init_fill(dm->type, off + dm->offset, es->then);
      es = es->next;
      continue;
    }
    if (m->is_bitfield)
      error("a bit-field cannot be initialized by a brace list");
    es = init_fill(m->type, off + m->offset, es);
  }
  /* a designator left over after the members ran out targets an
   * already-filled member: an out-of-order list re-fills it */
  while (es && es->kind == ND_DESIG) {
    if (es->lhs)
      error("array designator on a struct or union");
    Member *dm = find_member(ty, es->name);
    if (!dm)
      error("no member named '%s'", es->name);
    if (dm->is_bitfield)
      error("a bit-field cannot be initialized by a brace list");
    init_fill(dm->type, off + dm->offset, es->then);
    es = es->next;
  }
  return es;
}

/* fill one element slot of type ty at off from the element es; a
 * braced element covers exactly one slot (its list feeds the slot's
 * own members), an unbraced scalar auto-braces into an aggregate slot */
static Node *init_fill(Type *ty, int off, Node *es) {
  if (es && es->kind == ND_DESIG) {
    /* a designator fills exactly the slot it names inside ty and
     * consumes one list element; a chain (.a.b[2]) walks down. each
     * step descends into an aggregate, so its other parts stay zero */
    Node *d = es;
    for (;;) {
      if (d->lhs) {
        if (ty->kind != TY_ARRAY)
          error("array designator on a non-array");
        if (!is_const_expr(d->lhs))
          error("array designator is not a constant expression");
        CVal cv = const_fold(d->lhs);
        if (cv.is_float)
          error("array designator is not an integer constant expression");
        int idx = cv.val;
        if (!init_counting && (idx < 0 || idx >= ty->array_len))
          error("array designator [%d] out of range for [%d]", idx,
                ty->array_len);
        for (int j = 0; j < ty->array_len; j++) {
          if (j == idx)
            continue;
          if (is_agg(ty->base))
            init_fill_members(ty->base, off + j * type_size(ty->base), NULL);
          else
            init_add(ty->base, off + j * type_size(ty->base), NULL);
        }
        off += idx * type_size(ty->base);
        ty = ty->base;
      } else {
        Member *m = find_member(ty, d->name);
        if (!m)
          error("no member named '%s'", d->name);
        for (Member *o = ty->members; o; o = o->next) {
          if (o == m)
            continue;
          if (is_agg(o->type))
            init_fill_members(o->type, off + o->offset, NULL);
          else
            init_add(o->type, off + o->offset, NULL);
        }
        off += m->offset;
        ty = m->type;
      }
      if (!d->then || d->then->kind != ND_DESIG)
        break;
      d = d->then;
    }
    init_fill(ty, off, d->then);
    return es->next;
  }
  if (es && es->kind == ND_INIT_LIST) {
    if (!is_agg(ty)) {
      /* braced scalar, like int x = {5} */
      if (!es->elems || es->elems->next)
        error("excess elements in initializer");
      init_add(ty, off, es->elems);
      return es->next;
    }
    if (init_fill_members(ty, off, es->elems))
      error("excess elements in initializer");
    return es->next;
  }

  if (is_agg(ty))
    return init_fill_members(ty, off, es);

  if (!es) {
    init_add(ty, off, NULL);   /* zero */
    return NULL;
  }
  if (es->kind == ND_STR) {
    /* a string only fills char arrays (handled above) and char
     * pointers, where it means the address of the literal */
    if (ty->kind != TY_PTR)
      error("invalid string initializer");
  }
  init_add(ty, off, es);
  return es->next;
}

/* how many elements the flexible array ty needs, i.e. one past the
 * last slot any element (or array designator) reaches; runs in
 * counting mode so no leaves are produced */
static int init_len(Type *ty, Node *es) {
  init_counting = 1;
  int n = 0;    /* largest position reached, i.e. the length */
  int pos = 0;  /* the sequential fill position */
  Node *e = es;
  while (e) {
    if (e->kind == ND_DESIG && e->lhs) {
      /* an array designator pins the position at idx+1 */
      if (!is_const_expr(e->lhs))
        error("array designator is not a constant expression");
      CVal cv = const_fold(e->lhs);
      if (cv.is_float)
        error("array designator is not an integer constant expression");
      if (cv.val < 0)
        error("array designator out of range");
      pos = cv.val + 1;
      init_fill(ty->base, 0, e->then);
      e = e->next;
    } else {
      pos++;
      e = init_fill(ty->base, 0, e);
    }
    if (pos > n)
      n = pos;
  }
  init_counting = 0;
  return n;
}

/* entry point: deduce a flexible array's length, then flatten the
 * initializer into n->inits and type the leaves */
static void resolve_initializer(Node *n) {
  Type *ty = n->type;

  if (ty->kind == TY_ARRAY && ty->array_len == 0) {
    if (n->init->kind == ND_STR) {
      ty->array_len = n->init->str_len + 1;
    } else {
      int per = init_scalars(ty->base);
      if (per == 0)
        error("unsupported nested flexible array");
      ty->array_len = init_len(ty, n->init->elems);
      if (ty->array_len == 0)
        error("empty flexible array initializer");
    }
    ty->size = type_size(ty);
  }

  init_leaf_n = 0;
  if (n->init->kind == ND_STR) {
    if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR) {
      init_fill_members(ty, 0, n->init);
    } else if (ty->kind == TY_PTR) {
      /* "char *p = \"x\";" is the address of the literal */
      init_fill(ty, 0, n->init);
    } else {
      error("invalid string initializer");
    }
  } else {
    init_fill(ty, 0, n->init);
  }

  n->inits = xmalloc(sizeof(Init) * init_leaf_n);
  memcpy(n->inits, init_leafs, sizeof(Init) * init_leaf_n);
  n->init_n = init_leaf_n;

  for (int i = 0; i < n->init_n; i++) {
    Init *it = &n->inits[i];
    if (!it->expr)
      continue;
    resolve_expr(it->expr);
    if (is_real(it->ty) && it->ty->kind != it->expr->type->kind)
      it->expr = cast_of(it->expr, it->ty);
    else if (!is_real(it->ty) && is_real(it->expr->type))
      it->expr = cast_of(it->expr, it->ty);
  }
}

/* walk the statement tree of a switch body, calling fn on every
 * case/default label in source order. the tree is not modified: the
 * labels keep their place in the body and gen_stmt emits them where
 * they sit (C11 6.8.4.2p5 lets them hide in blocks and branches).
 * a label's body hangs off the label node (it is never part of the
 * enclosing chain), so both positions are visited exactly once.
 * nested switches own their own labels. */
static void walk_cases(Node *s, void (*fn)(Node *, void *), void *arg) {
  for (; s; s = s->next) {
    if (s->kind == ND_CASE) {
      fn(s, arg);
      /* the body hangs off the label (next is NULL unless it is a
       * declaration chain), never in this chain, so walk it here */
      if (s->body)
        walk_cases(s->body, fn, arg);
      continue;
    }
    switch (s->kind) {
      case ND_BLOCK:
        walk_cases(s->body, fn, arg);
        break;
      case ND_IF:
        walk_cases(s->then, fn, arg);
        if (s->els)
          walk_cases(s->els, fn, arg);
        break;
      case ND_WHILE:
      case ND_DO_WHILE:
      case ND_FOR:
        walk_cases(s->then, fn, arg);
        break;
      case ND_LABEL:
        if (s->body)
          walk_cases(s->body, fn, arg);
        break;
      case ND_SWITCH:
        break;
      default:
        break;
    }
  }
}

/* case-label validation state for one switch: the [lo, hi] spans seen
 * so far (a plain "case N:" is the span [N, N]) and whether a
 * default has been seen */
typedef struct {
  int *seen;       /* pairs: [lo0, hi0, lo1, hi1, ...] */
  int n, cap;
  int seen_def;
} CaseCheck;

static void check_case(Node *c, void *arg) {
  CaseCheck *cs = arg;
  if (!c->lhs) {
    if (cs->seen_def)
      error("duplicate default label");
    cs->seen_def = 1;
    return;
  }
  resolve_expr(c->lhs);
  if (!is_const_expr(c->lhs))
    error("case label is not a constant");
  CVal v = const_fold(c->lhs);
  if (v.is_float)
    error("case label is not an integer constant");
  CVal w = v;
  if (c->rhs) {
    /* a GNU case range: both ends must be constants and ordered */
    resolve_expr(c->rhs);
    if (!is_const_expr(c->rhs))
      error("case range end is not a constant");
    w = const_fold(c->rhs);
    if (w.is_float)
      error("case range end is not an integer constant");
    if (v.val > w.val)
      error("case range is empty");
  }
  for (int i = 0; i < cs->n; i++)
    /* closed spans overlap when neither starts past the other's end */
    if (v.val <= cs->seen[2 * i + 1] && cs->seen[2 * i] <= w.val)
      error("duplicate case value");
  if (cs->n + 1 > cs->cap) {
    cs->cap = cs->cap ? cs->cap * 2 : 8;
    /* each span is a pair of ints */
    cs->seen = xrealloc(cs->seen, sizeof(int) * cs->cap * 2);
  }
  cs->seen[2 * cs->n] = v.val;
  cs->seen[2 * cs->n + 1] = w.val;
  cs->n++;
}

/* a _Noreturn function must not return; a return statement anywhere
 * in the body would be the compiler writing code that ends in
 * undefined behavior (C11 6.7.4p8), so it is rejected out of hand */
static int contains_return(Node *s) {
  for (; s; s = s->next) {
    switch (s->kind) {
      case ND_RETURN:
        return 1;
      case ND_BLOCK:
      case ND_SWITCH:
        if (contains_return(s->body))
          return 1;
        break;
      case ND_IF:
        if (contains_return(s->then))
          return 1;
        if (s->els && contains_return(s->els))
          return 1;
        break;
      case ND_WHILE:
      case ND_DO_WHILE:
      case ND_FOR:
      case ND_CASE:
      case ND_LABEL:
        if (contains_return(s->then))
          return 1;
        break;
      default:
        break;
    }
  }
  return 0;
}

static void resolve_stmt(Node *n) {
  switch (n->kind) {
    case ND_BLOCK:
      resolve_block(n);
      return;
    case ND_LABEL:
      resolve_stmt(n->body);
      return;
    case ND_GOTO:
      return;
    case ND_DECL: {
      if (n->is_extern)
        error("storage class on a local variable, unsupported");
      check_type_supported(n->type);
      if (n->type->kind == TY_VOID)
        error("variable '%s' declared void", n->name);
      if (type_is_vla(n->type)) {
        /* a VLA has no compile-time size: no static storage, no
         * initializer (6.7.5.2), and its dimension expressions have
         * to be resolved so the allocation can evaluate them */
        if (n->is_static)
          error("variable '%s': a variable-length array cannot be static", n->name);
        if (n->init)
          error("variable '%s': a variable-length array cannot be initialized", n->name);
        for (Type *dt = n->type; dt->kind == TY_ARRAY; dt = dt->base)
          if (dt->vla_len)
            resolve_expr(dt->vla_len);
        n->vla_sz = vla_size_expr(n->type);
        resolve_expr(n->vla_sz);
      }
      if (n->init) {
        if (n->type->kind == TY_ARRAY &&
            n->init->kind != ND_INIT_LIST && n->init->kind != ND_STR)
          error("invalid array initializer");
        if (n->init->kind == ND_INIT_LIST ||
            (n->init->kind == ND_STR && n->type->kind == TY_ARRAY)) {
          /* brace/string initializers run before the slot is laid
           * out, so a flexible array can decide its own length */
          resolve_initializer(n);
        } else {
          resolve_expr(n->init);
          if (is_real(n->type) && n->type->kind != n->init->type->kind)
            n->init = cast_of(n->init, n->type);
          else if (!is_real(n->type) && is_real(n->init->type))
            n->init = cast_of(n->init, n->type);
        }
      }
      Obj *o = new_obj(n->name, n->type);
      o->align = n->align;
      if (n->is_static) {
        /* a function-local static is a data symbol like a global; the
         * name gets mangled so the same identifier in two functions
         * doesn't collide. the initializer (resolved above) lands in
         * .data/.bss through gen_data at codegen time, not in a stack
         * slot */
        char *mn = xmalloc(9 + strlen(n->name) + 1);
        sprintf(mn, "static%d_%s", static_decls_n, n->name);
        o->symname = mn;
        o->is_local = 0;
        if (static_decls_n == static_decls_cap) {
          static_decls_cap = static_decls_cap ? static_decls_cap * 2 : 16;
          static_decls = xrealloc(static_decls,
                                  sizeof(Node *) * static_decls_cap);
        }
        static_decls[static_decls_n++] = n;
      } else {
        o->is_local = 1;
        /* a VLA takes two 8-byte slots: a pointer to the storage the
         * declaration carves out of the stack at run time, and the
         * size it captured there; its size is 0 as a type */
        int sz = type_is_vla(o->type) ? 16 : o->type->size;
        int a = type_align(o->type);
        if (o->align > a)
          a = o->align;
        if (a < 8)
          a = 8;
        cur_offset -= roundup(sz, a);
        align_snap(&cur_offset, a);
        o->offset = cur_offset;
        if (type_is_vla(o->type))
          o->size_off = cur_offset + 8;
      }
      push_var(o);
      n->var = o;
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
    case ND_SWITCH: {
      CaseCheck st = {0};
      resolve_expr(n->cond);
      walk_cases(n->body, check_case, &st);
      resolve_stmt(n->body);
      return;
    }
    case ND_CASE:
      resolve_stmt(n->body);
      return;
    case ND_RETURN:
      if (n->lhs) {
        resolve_expr(n->lhs);
        if (cur_ret->kind == TY_STRUCT || cur_ret->kind == TY_UNION) {
          if (n->lhs->type->kind != TY_STRUCT &&
              n->lhs->type->kind != TY_UNION)
            error("incompatible return type");
          n->var = cur_sret;
        } else if (is_real(cur_ret) && cur_ret->kind != n->lhs->type->kind)
          n->lhs = cast_of(n->lhs, cur_ret);
        else if (!is_real(cur_ret) && is_real(n->lhs->type))
          n->lhs = cast_of(n->lhs, cur_ret);
        else if (cur_ret->is_bool && !n->lhs->type->is_bool) {
          /* cast_of skips int->int, but _Bool is a real conversion:
           * any nonzero value must become 1 in the return register */
          Node *c = xmalloc(sizeof(Node));
          c->kind = ND_CAST;
          c->lhs = n->lhs;
          c->targ = cur_ret;
          c->type = cur_ret;
          n->lhs = c;
        }
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
  static_decls_n = 0;
  cur_ret = NULL;   /* a fresh file starts outside every function */

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
      o->align = n->align;
    } else {
      error("internal: unexpected top-level node");
      return;
    }
    Obj *prev = find_var(n->name);
    if (prev) {
      /* first declaration wins (proto + definition, extern then
       * def); link the node anyway, codegen needs the symbol */
      n->var = prev;
      continue;
    }
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
      if (n->init) {
        if (n->type->kind == TY_ARRAY &&
            n->init->kind != ND_INIT_LIST && n->init->kind != ND_STR)
          error("invalid array initializer");
        if (n->init->kind == ND_INIT_LIST ||
            (n->init->kind == ND_STR && n->type->kind == TY_ARRAY))
          resolve_initializer(n);
        else
          resolve_expr(n->init);
      }
      continue;
    }

    Type *ft = n->type;
    check_type_supported(ft->ret);
    if (ft->ret->kind == TY_STRUCT || ft->ret->kind == TY_UNION) {
      /* struct-returning functions take a hidden first parameter: a
       * pointer to the caller's return buffer. "~" can never start a
       * real identifier, so the name cannot collide */
      Node *sret = xmalloc(sizeof(Node));
      memset(sret, 0, sizeof(Node));
      sret->kind = ND_DECL;
      sret->name = "~ret";
      sret->type = ptr_to(ft->ret);
      sret->next = ft->params;
      ft->params = sret;
    }
    for (Node *p = ft->params; p; p = p->next)
      check_type_supported(p->type);

    if (!n->body)
      continue;   /* prototype only */

    enter_scope();
    cur_offset = 0;
    cur_ret = ft->ret;
    cur_sret = NULL;
    cur_va_off = 0;
    for (Node *p = ft->params; p; p = p->next) {
      Obj *po = new_obj(p->name, p->type);
      po->is_local = 1;
      /* array parameters decay to pointers, per C */
      if (po->type->kind == TY_ARRAY) {
        po->type = ptr_to(po->type->base);
        p->type = po->type;
      }
      if (p->name && strcmp(p->name, "~ret") == 0)
        cur_sret = po;
      int a = type_align(po->type) > 8 ? type_align(po->type) : 8;
      cur_offset -= roundup(po->type->size, a);
      align_snap(&cur_offset, a);
      po->offset = cur_offset;
      push_var(po);
      p->var = po;
    }
    if (ft->is_variadic) {
      /* the register save area: a 176-byte local (6 GP regs, then
       * 8 xmm regs in 16-byte slots) that the prologue fills from
       * the incoming argument registers; va_start points its
       * reg_save_area at it */
      Obj *vo = new_obj("~va", array_of(type_new(TY_CHAR), 176));
      vo->is_local = 1;
      cur_offset -= roundup(vo->type->size, 8);
      align_snap(&cur_offset, 8);
      vo->offset = cur_offset;
      push_var(vo);
      n->val = vo->offset;
      cur_va_off = vo->offset;
    }
    resolve_block(n->body);
    if (n->is_noreturn && contains_return(n->body))
      error("function '%s' declared _Noreturn should not return", n->name);
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

/* float constants get their own pool: the bits are half the width,
 * and the labels must not collide with the double ones */
static float fconsts[256];
static int fconsts_n;

static void emit_constf(float f) {
  for (int i = 0; i < fconsts_n; i++)
    if (fconsts[i] == f) {
      fprintf(out, "  movss .LCf%d(%%rip), %%xmm0\n", i);
      return;
    }
  if (fconsts_n == 256)
    error("too many floating point constants");
  fconsts[fconsts_n] = f;
  unsigned bits;
  memcpy(&bits, &f, 4);
  fprintf(out, "  movss .LCf%d(%%rip), %%xmm0\n", fconsts_n);
  section(".rodata");
  fprintf(out, ".LCf%d:\n  .long 0x%x\n", fconsts_n, bits);
  section(".text");
  fconsts_n++;
}

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

/* load the value at (%rax) into %rax (ints) or %xmm0 (reals),
 * sign or zero extending to fit the declared type. this is where
 * signedness enters the register */
static void load(Type *t) {  if (t->kind == TY_DOUBLE) {
    fprintf(out, "  movsd (%%rax), %%xmm0\n");
    return;
  }
  if (t->kind == TY_FLOAT) {
    fprintf(out, "  movss (%%rax), %%xmm0\n");
    return;
  }
  switch (t->size) {
    case 1:
      fprintf(out, "  %s (%%rax), %%eax\n",
              (t->is_unsigned || t->is_bool) ? "movzbl" : "movsbl");
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

/* store %rax (ints) or %xmm0 (reals) at (%rdi) */
static void store(Type *t) {
  if (t->kind == TY_DOUBLE) {
    fprintf(out, "  movsd %%xmm0, (%%rdi)\n");
    return;
  }
  if (t->kind == TY_FLOAT) {
    fprintf(out, "  movss %%xmm0, (%%rdi)\n");
    return;
  }
  switch (t->size) {
    case 1:
      if (t->is_bool) {
        /* any nonzero value becomes 1 (C99 6.3.1.2) */
        fprintf(out, "  test %%al, %%al\n");
        fprintf(out, "  setne %%al\n");
      }
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

/* %rax = the address of the storage unit; %rax becomes the value of
 * the bit-field (n->bit_width bits at n->bit_offset). the unit is
 * loaded whole -- sign does not matter, the shifts flush the bits
 * above the field -- then the field is sign or zero extended */
static void gen_bitfield_load(Node *n) {
  int b = n->bit_offset;
  int w = n->bit_width;
  switch (n->type->size) {
    case 1:
      fprintf(out, "  movzbl (%%rax), %%eax\n");
      break;
    case 2:
      fprintf(out, "  movzwl (%%rax), %%eax\n");
      break;
    case 4:
      fprintf(out, "  movl (%%rax), %%eax\n");
      break;
    default:
      fprintf(out, "  movq (%%rax), %%rax\n");
      break;
  }
  if (b)
    fprintf(out, "  shrq $%d, %%rax\n", b);
  if (w < 64) {
    fprintf(out, "  shlq $%d, %%rax\n", 64 - w);
    fprintf(out, "  %s $%d, %%rax\n",
            n->type->is_unsigned || n->type->is_bool ? "shrq" : "sarq",
            64 - w);
  }
}

/* %rdi = address of the storage unit, %rax = the value to store.
 * merges the field into the unit with a read-modify-write. %rax ends
 * with the plain stored value (the expression's value), and %rcx is
 * untouched so postfix ++/-- can hold the old value in it */
static void gen_bitfield_store(Node *n) {
  int b = n->bit_offset;
  int w = n->bit_width;
  if (n->type->is_bool) {
    /* _Bool stores normalize to 0/1; the raw mask below would turn
     * a 2 stored into a 1-bit field into 0 */
    fprintf(out, "  test %%rax, %%rax\n");
    fprintf(out, "  setne %%al\n");
    fprintf(out, "  movzbq %%al, %%rax\n");
  }
  if (w < 64) {
    /* flush the garbage above the field width and restore the field
     * into [0,w): the register value of the assignment */
    fprintf(out, "  shlq $%d, %%rax\n", 64 - w);
    fprintf(out, "  shrq $%d, %%rax\n", 64 - w);
  }
  fprintf(out, "  push %%rax\n");   /* [plain value] */
  if (b)
    fprintf(out, "  shlq $%d, %%rax\n", b);   /* into place */
  fprintf(out, "  push %%rax\n");   /* [plain value, positioned] */
  switch (n->type->size) {
    case 1:
      fprintf(out, "  movzbl (%%rdi), %%edx\n");
      break;
    case 2:
      fprintf(out, "  movzwl (%%rdi), %%edx\n");
      break;
    case 4:
      fprintf(out, "  movl (%%rdi), %%edx\n");
      break;
    default:
      fprintf(out, "  movq (%%rdi), %%rdx\n");
      break;
  }
  if (w < 64) {
    /* (1 << w) - 1: ones over [0,w), shifted to [b, b+w); the
     * notq below leaves ones everywhere else */
    fprintf(out, "  mov $1, %%rax\n");
    fprintf(out, "  shlq $%d, %%rax\n", w);
    fprintf(out, "  subq $1, %%rax\n");
  } else {
    fprintf(out, "  mov $0, %%rax\n");
  }
  if (b)
    fprintf(out, "  shlq $%d, %%rax\n", b);
  fprintf(out, "  notq %%rax\n");   /* unit mask with the field cleared */
  fprintf(out, "  andq %%rax, %%rdx\n");
  fprintf(out, "  pop %%rax\n");    /* the positioned value */
  fprintf(out, "  orq %%rax, %%rdx\n");
  switch (n->type->size) {
    case 1:
      fprintf(out, "  movb %%dl, (%%rdi)\n");
      break;
    case 2:
      fprintf(out, "  movw %%dx, (%%rdi)\n");
      break;
    case 4:
      fprintf(out, "  movl %%edx, (%%rdi)\n");
      break;
    default:
      fprintf(out, "  movq %%rdx, (%%rdi)\n");
      break;
  }
  fprintf(out, "  pop %%rax\n");    /* the plain stored value */
  if (w < 64) {
    /* widen the stored value to the field's signedness, the same
     * way the load does: the raw int is not the field's value */
    fprintf(out, "  shlq $%d, %%rax\n", 64 - w);
    fprintf(out, "  %s $%d, %%rax\n",
            n->type->is_unsigned || n->type->is_bool ? "shrq" : "sarq",
            64 - w);
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
      if (o->is_local) {
        if (type_is_vla(o->type))
          /* the slot holds the pointer the declaration stored */
          fprintf(out, "  mov %d(%%rbp), %%rax\n", o->offset);
        else
          fprintf(out, "  lea %d(%%rbp), %%rax\n", o->offset);
      } else
        fprintf(out, "  lea %s(%%rip), %%rax\n", o->symname ? o->symname : o->name);
      return;
    }
    case ND_COMP_LIT:
      /* the hidden local of a compound literal is always in this
       * frame. the initializer stores run on every path that takes
       * the address -- the value load, member/index access, and
       * address-of -- so they live here, not in gen_expr */
      if (n->inits)
        gen_init_stores(n);
      fprintf(out, "  lea %d(%%rbp), %%rax\n", n->var->offset);
      return;
    case ND_INDEX: {
      gen_expr(n->lhs);
      fprintf(out, "  push %%rax\n");
      gen_expr(n->rhs);
      if (n->vla_sz) {
        /* a[i], where the element type is itself variable-length
         * (int a[n][m]): the stride is m's byte size, evaluated now */
        fprintf(out, "  push %%rax\n");
        gen_expr(n->vla_sz);
        fprintf(out, "  mov %%rax, %%rcx\n");
        fprintf(out, "  pop %%rax\n");
        fprintf(out, "  imul %%rcx, %%rax\n");
      } else {
        int sz = type_size(n->lhs->type->base);
        if (sz > 1)
          fprintf(out, "  imul $%d, %%rax, %%rax\n", sz);
      }
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
    case ND_GENERIC:
      /* the selection is an lvalue exactly when the chosen arm is */
      gen_addr(n->then);
      return;
    default:
      error("not an lvalue");
  }
}

/* combine the real values in xmm0 (lhs) and xmm1 (rhs); both
 * operands are the same kind after resolve's promotions */
static void emit_fp_combine(int op, int is_dbl) {
  const char *mv = is_dbl ? "movsd" : "movss";
  const char *cmp = is_dbl ? "ucomisd" : "ucomiss";

  switch (op) {
    case '+':   fprintf(out, "  add%s %%xmm1, %%xmm0\n", is_dbl ? "sd" : "ss"); return;
    case '-':   fprintf(out, "  sub%s %%xmm1, %%xmm0\n", is_dbl ? "sd" : "ss"); return;
    case '*':   fprintf(out, "  mul%s %%xmm1, %%xmm0\n", is_dbl ? "sd" : "ss"); return;
    case '/':   fprintf(out, "  div%s %%xmm1, %%xmm0\n", is_dbl ? "sd" : "ss"); return;
    case OP_LOGAND:
    case OP_LOGOR: {
      /* no short-circuiting; normalize both to 0/1 in int regs */
      fprintf(out, "  %s %%xmm0, %%xmm3\n", mv);
      fprintf(out, "  %s %%xmm1, %%xmm4\n", mv);
      if (is_dbl)
        emit_const(0.0);
      else
        emit_constf(0.0f);
      fprintf(out, "  %s %%xmm0, %%xmm2\n", mv);
      fprintf(out, "  %s %%xmm2, %%xmm3\n", cmp);
      fprintf(out, "  setne %%al\n");
      fprintf(out, "  movzbq %%al, %%rax\n");
      fprintf(out, "  %s %%xmm2, %%xmm4\n", cmp);
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
      fprintf(out, "  %s %%xmm1, %%xmm0\n", cmp);
      fprintf(out, "  %s %%al\n", set);
      fprintf(out, "  movzbq %%al, %%rax\n");
      return;
    }
  }
}

/* combine %rax/%xmm0 (lhs) with %rdi/%xmm1 (rhs), result in
 * %rax (ints) or %xmm0 (reals, promotions done in resolve).
 * pointers get their integer operand scaled on the way in */
static void emit_combine(int op, Type *lhs_ty, Type *rhs_ty) {
  if (is_real(lhs_ty) || is_real(rhs_ty)) {
    emit_fp_combine(op, lhs_ty->kind == TY_DOUBLE ||
                        rhs_ty->kind == TY_DOUBLE);
    return;
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
      /* short-circuiting is handled in gen_bin; this path just
       * normalizes the values to 0/1 and combines them */
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
  /* struct-returning callees write into a hidden buffer in our own
   * frame (allocated in resolve); the buffer's address is passed as
   * the first argument, and is also the call's result value. a bare
   * struct-typed ND_VAR evaluates to its address, so the synthetic
   * argument needs no extra & */
  int has_sret = n->var && (n->type->kind == TY_STRUCT || n->type->kind == TY_UNION);
  int nargs = has_sret;
  for (Node *a = n->args; a; a = a->next)
    nargs++;
  Node **args = xmalloc(sizeof(Node *) * nargs);
  Node sret_arg = {0};
  int i = 0;
  if (has_sret) {
    sret_arg.kind = ND_VAR;
    sret_arg.var = n->var;
    sret_arg.type = n->type;
    args[i++] = &sret_arg;
  }
  for (Node *a = n->args; a; a = a->next)
    args[i++] = a;

  /* SysV: ints go to rdi..r9, doubles to xmm0..7, each class counted
   * independently and in argument order; anything past the budget
   * lands on the stack, still in argument order. float operands are
   * promoted to double here, so a float parameter is a double on the
   * wire (our own convention, both sides agree) */
  static char *intregs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
  int nsse = 0;
  {
    int ireg = 0, sreg = 0;
    for (i = 0; i < nargs; i++)
      if (is_real(args[i]->type)) {
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
    if (args[i]->type->kind == TY_FLOAT)
      fprintf(out, "  cvtss2sd %%xmm0, %%xmm0\n");
    if (is_real(args[i]->type)) {
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
    if (is_real(args[i]->type)) {
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
    if (is_real(args[i]->type))
      is_stk = sreg2++ >= 8;
    else
      is_stk = ireg2++ >= 6;
    if (is_stk) {
      if (is_real(args[i]->type)) {
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

  /* the call's value: the address of the return buffer */
  if (has_sret)
    fprintf(out, "  lea %d(%%rbp), %%rax\n", n->var->offset);
}

/* evaluate a condition; jump to lbl when it is false */
static void gen_cond_jump_false(Node *cond, int lbl) {
  gen_expr(cond);
  if (cond->type->kind == TY_FLOAT) {
    fprintf(out, "  movss %%xmm0, %%xmm2\n");
    emit_constf(0.0f);
    fprintf(out, "  movss %%xmm2, %%xmm1\n");
    fprintf(out, "  ucomiss %%xmm1, %%xmm0\n");
    fprintf(out, "  setne %%al\n");
    fprintf(out, "  movzbq %%al, %%rax\n");
  } else if (cond->type->kind == TY_DOUBLE) {
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
  if (cond->type->kind == TY_FLOAT) {
    fprintf(out, "  movss %%xmm0, %%xmm2\n");
    emit_constf(0.0f);
    fprintf(out, "  movss %%xmm2, %%xmm1\n");
    fprintf(out, "  ucomiss %%xmm1, %%xmm0\n");
    fprintf(out, "  setne %%al\n");
    fprintf(out, "  movzbq %%al, %%rax\n");
  } else if (cond->type->kind == TY_DOUBLE) {
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
      if (n->is_float) {
        if (n->is_f)
          emit_constf((float)n->fval);
        else
          emit_const(n->fval);
      } else if (n->type->is_unsigned) {
        /* val is stored truncated to 32 bits; an unsigned constant
         * must ride the register zero-extended, so 0xdeadbeef is
         * 0xdeadbeef and not sign-extended to 0xffffffffdeadbeef.
         * the 32-bit destination makes the mov a zero-extend */
        fprintf(out, "  mov $%u, %%eax\n", (unsigned)n->val);
      } else {
        fprintf(out, "  mov $%d, %%rax\n", n->val);
      }
      return;
    case ND_STR:
      emit_string(n);
      fprintf(out, "  lea %s(%%rip), %%rax\n", n->var->symname ? n->var->symname : n->var->name);
      return;
    case ND_VAR:
      gen_addr(n);
      if (n->type->kind != TY_ARRAY && n->type->kind != TY_FUNC &&
          n->type->kind != TY_STRUCT && n->type->kind != TY_UNION)
        load(n->type);
      return;
    case ND_ASSIGN: {
      gen_addr(n->lhs);
      fprintf(out, "  push %%rax\n");

      if (n->op == '=' && (n->lhs->type->kind == TY_STRUCT ||
                           n->lhs->type->kind == TY_UNION)) {
        /* whole-struct assignment is a memcpy; the value of the
         * expression is &lhs. the pushed address leaves rsp 8 off the
         * SysV alignment, so a filler goes below it */
        gen_expr(n->rhs);
        fprintf(out, "  mov %%rax, %%rsi\n");
        fprintf(out, "  mov (%%rsp), %%rdi\n");
        fprintf(out, "  mov $%d, %%rdx\n", n->lhs->type->size);
        fprintf(out, "  sub $8, %%rsp\n");
        fprintf(out, "  call memcpy\n");
        fprintf(out, "  add $8, %%rsp\n");
        fprintf(out, "  pop %%rax\n");
        return;
      }

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
        if (is_real(n->type)) {
          /* real compound: park the rhs, then load the lhs */
          const char *mv = n->type->kind == TY_DOUBLE ? "movsd" : "movss";
          gen_expr(n->rhs);
          fprintf(out, "  sub $8, %%rsp\n");
          fprintf(out, "  %s %%xmm0, (%%rsp)\n", mv);
          gen_expr(n->lhs);
          fprintf(out, "  %s (%%rsp), %%xmm1\n", mv);
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
      if (n->lhs->is_bitfield)
        gen_bitfield_store(n->lhs);
      else
        store(n->lhs->type);
      return;
    }
    case ND_BIN:
      if (n->op == OP_LOGAND || n->op == OP_LOGOR) {
        /* short-circuit: && skips the rhs when the lhs is 0, ||
         * when it's 1; the result is normalized to 0/1 */
        int lz = labeln++;
        int le = labeln++;
        gen_expr(n->lhs);
        if (n->op == OP_LOGAND) {
          fprintf(out, "  cmp $0, %%rax\n");
          fprintf(out, "  je .L%d\n", lz);
          gen_expr(n->rhs);
          fprintf(out, "  cmp $0, %%rax\n");
          fprintf(out, "  setne %%al\n");
          fprintf(out, "  movzbq %%al, %%rax\n");
          fprintf(out, "  jmp .L%d\n", le);
          fprintf(out, ".L%d:\n", lz);
          fprintf(out, "  mov $0, %%rax\n");
          fprintf(out, ".L%d:\n", le);
        } else {
          fprintf(out, "  cmp $0, %%rax\n");
          fprintf(out, "  jne .L%d\n", le);
          gen_expr(n->rhs);
          fprintf(out, "  cmp $0, %%rax\n");
          fprintf(out, "  setne %%al\n");
          fprintf(out, "  movzbq %%al, %%rax\n");
          fprintf(out, ".L%d:\n", le);
        }
        return;
      }
      if (n->op == ',') {
        /* evaluate the left side for its effects, keep the right */
        gen_expr(n->lhs);
        gen_expr(n->rhs);
        return;
      }
      gen_expr(n->rhs);
      if (is_real(n->lhs->type) || is_real(n->rhs->type)) {
        /* the rhs has to survive the lhs evaluation, which may itself
         * use the xmm registers, so park it on the stack */
        const char *mv = n->lhs->type->kind == TY_DOUBLE ||
                         n->rhs->type->kind == TY_DOUBLE ?
                         "movsd" : "movss";
        fprintf(out, "  sub $8, %%rsp\n");
        fprintf(out, "  %s %%xmm0, (%%rsp)\n", mv);
        gen_expr(n->lhs);
        fprintf(out, "  %s (%%rsp), %%xmm1\n", mv);
        fprintf(out, "  add $8, %%rsp\n");
      } else {
        fprintf(out, "  push %%rax\n");
        gen_expr(n->lhs);
        fprintf(out, "  pop %%rdi\n");
      }
      emit_combine(n->op, n->lhs->type, n->rhs->type);
      return;
    case ND_CAST: {
      gen_expr(n->lhs);
      int fr = n->lhs->type->kind, to = n->targ->kind;
      if (fr == TY_FLOAT && to == TY_DOUBLE)
        fprintf(out, "  cvtss2sd %%xmm0, %%xmm0\n");
      else if (fr == TY_DOUBLE && to == TY_FLOAT)
        fprintf(out, "  cvtsd2ss %%xmm0, %%xmm0\n");
      else if (fr == TY_FLOAT)
        fprintf(out, "  cvttss2si %%xmm0, %%rax\n");
      else if (to == TY_FLOAT)
        fprintf(out, "  cvtsi2ss %%rax, %%xmm0\n");
      else if (fr == TY_DOUBLE)
        fprintf(out, "  cvttsd2si %%xmm0, %%rax\n");
      else if (is_real(n->targ))
        fprintf(out, "  cvtsi2sd %%rax, %%xmm0\n");
      if (!is_real(n->targ) && !is_real(n->lhs->type)) {
        /* integer casts: narrow or re-sign the value in %rax. the
         * memory loads sign-extend, so an unsigned target has to be
         * zero-extended and a narrowing target re-trimmed */
        int tosz = n->targ->size;
        int frsz = n->lhs->type->size;
        if (n->targ->is_unsigned && (!n->lhs->type->is_unsigned ||
                                     tosz < frsz)) {
          if (tosz == 1)
            fprintf(out, "  movzbq %%al, %%rax\n");
          else if (tosz == 2)
            fprintf(out, "  movzwq %%ax, %%rax\n");
          else if (tosz == 4)
            fprintf(out, "  mov %%eax, %%eax\n");
        } else if (!n->targ->is_unsigned && tosz < frsz) {
          if (tosz == 1)
            fprintf(out, "  movsbq %%al, %%rax\n");
          else if (tosz == 2)
            fprintf(out, "  movswq %%ax, %%rax\n");
          else if (tosz == 4)
            fprintf(out, "  movslq %%eax, %%rax\n");
        }
      }
      if (n->targ->is_bool) {
        /* real sources hit cvtt*2si above, so the int result is in
         * %rax; collapse it to 0/1 */
        fprintf(out, "  test %%al, %%al\n");
        fprintf(out, "  setne %%al\n");
      }
      return;
    }
    case ND_UNARY:
      switch (n->op) {
        case '&':
          gen_addr(n->lhs);
          return;
        case '*':
          /* the operand is a pointer; its value is the address
           * (and *fp on a function pointer is the function) */
          gen_expr(n->lhs);
          if (n->type->kind != TY_FUNC && n->type->kind != TY_STRUCT &&
            n->type->kind != TY_UNION && n->type->kind != TY_ARRAY)
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
          } else if (n->type->kind == TY_FLOAT) {
            fprintf(out, "  movss %%xmm0, %%xmm2\n");
            emit_constf(0.0f);
            fprintf(out, "  movss %%xmm2, %%xmm1\n");
            fprintf(out, "  subss %%xmm1, %%xmm0\n");
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
          } else if (n->lhs->type->kind == TY_FLOAT) {
            fprintf(out, "  movss %%xmm0, %%xmm2\n");
            emit_constf(0.0f);
            fprintf(out, "  movss %%xmm2, %%xmm1\n");
            fprintf(out, "  ucomiss %%xmm1, %%xmm0\n");
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
          if (n->lhs->kind == ND_MEMBER && n->lhs->is_bitfield)
            gen_bitfield_load(n->lhs);
          else
            load(n->type);
          if (n->type->kind == TY_DOUBLE) {
            /* reals go through xmm0; the old value is kept in
             * xmm1 for the postfix result, and pointers step a
             * scaled constant */
            fprintf(out, "  movsd %%xmm0, %%xmm1\n");
            emit_const(1.0 * scale);
            fprintf(out, "  movsd %%xmm1, %%xmm2\n");
            if (n->op == OP_INC)
              fprintf(out, "  addsd %%xmm2, %%xmm0\n");
            else {
              fprintf(out, "  subsd %%xmm0, %%xmm2\n");
              fprintf(out, "  movsd %%xmm2, %%xmm0\n");
            }
            fprintf(out, "  pop %%rdi\n");         /* addr */
            fprintf(out, "  movsd %%xmm0, (%%rdi)\n");
            if (!n->is_prefix)
              fprintf(out, "  movsd %%xmm1, %%xmm0\n");
            return;
          }
          if (n->type->kind == TY_FLOAT) {
            fprintf(out, "  movss %%xmm0, %%xmm1\n");
            emit_constf(1.0f * scale);
            fprintf(out, "  movss %%xmm1, %%xmm2\n");
            if (n->op == OP_INC)
              fprintf(out, "  addss %%xmm2, %%xmm0\n");
            else {
              fprintf(out, "  subss %%xmm0, %%xmm2\n");
              fprintf(out, "  movss %%xmm2, %%xmm0\n");
            }
            fprintf(out, "  pop %%rdi\n");         /* addr */
            fprintf(out, "  movss %%xmm0, (%%rdi)\n");
            if (!n->is_prefix)
              fprintf(out, "  movss %%xmm1, %%xmm0\n");
            return;
          }
          if (n->is_prefix) {
            fprintf(out, "  %s $%d, %%rax\n",
                    n->op == OP_INC ? "add" : "sub", scale);
            fprintf(out, "  pop %%rdi\n");          /* addr */
            if (n->lhs->is_bitfield)
              gen_bitfield_store(n->lhs);
            else
              store(n->type);
          } else {
            fprintf(out, "  push %%rax\n");         /* [addr, old] */
            fprintf(out, "  %s $%d, %%rax\n",
                    n->op == OP_INC ? "add" : "sub", scale);
            fprintf(out, "  pop %%rcx\n");          /* old value */
            fprintf(out, "  pop %%rdi\n");          /* addr */
            if (n->lhs->is_bitfield)
              gen_bitfield_store(n->lhs);
            else
              store(n->type);
            fprintf(out, "  mov %%rcx, %%rax\n");   /* result: old */
          }
          return;
        }
        default:
          error("internal: bad unary op %d", n->op);
      }
      return;
    case ND_STMT_EXPR:
      /* the body's last statement (an expression) already left its
       * value in %rax / %xmm0, so the selection is just the block */
      for (Node *s = n->body; s; s = s->next)
        gen_stmt(s);
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
          n->type->kind != TY_STRUCT && n->type->kind != TY_UNION)
        load(n->type);
      return;
    case ND_MEMBER:
      gen_addr(n);
      if (n->is_bitfield) {
        gen_bitfield_load(n);
        return;
      }
      if (n->type->kind != TY_ARRAY && n->type->kind != TY_FUNC &&
          n->type->kind != TY_STRUCT && n->type->kind != TY_UNION)
        load(n->type);
      return;
    case ND_SIZEOF:
      if (n->vla_sz) {
        if (n->lhs && n->lhs->kind == ND_VAR && n->lhs->var->size_off)
          /* sizeof of a declared VLA is the size its declaration
           * captured, exactly as gcc computes it */
          fprintf(out, "  mov %d(%%rbp), %%rax\n", n->lhs->var->size_off);
        else
          gen_expr(n->vla_sz);
        return;
      }
      if (n->lhs)
        fprintf(out, "  mov $%d, %%rax\n", type_size(n->lhs->type));
      else
        fprintf(out, "  mov $%d, %%rax\n", type_size(n->targ));
      return;
    case ND_ALIGNOF:
      fprintf(out, "  mov $%d, %%rax\n", n->val);
      return;
    case ND_GENERIC:
      /* resolution spliced the chosen arm into n->then, so the
       * generic selection is just that arm's code */
      gen_expr(n->then);
      return;
    case ND_VA_START: {
      /* ap gets the register counts va_start computed at parse time,
       * plus the addresses of the first stack vararg (16(%rbp)..)
       * and of this function's ~va register save area */
      gen_expr(n->lhs);
      fprintf(out, "  mov $%d, (%%rax)\n", n->va[0]);
      fprintf(out, "  mov $%d, 4(%%rax)\n", n->va[1]);
      fprintf(out, "  lea %d(%%rbp), %%rdx\n", n->va[2]);
      fprintf(out, "  mov %%rdx, 8(%%rax)\n");
      fprintf(out, "  lea %d(%%rbp), %%rdx\n", n->va[3]);
      fprintf(out, "  mov %%rdx, 16(%%rax)\n");
      return;
    }
    case ND_COMP_LIT:
      /* the expression's value is the object's address; the load
       * below mirrors ND_VAR: arrays/functions/aggregates keep the
       * address, scalars get their value */
      gen_addr(n);
      if (n->type->kind != TY_ARRAY && n->type->kind != TY_FUNC &&
          n->type->kind != TY_STRUCT && n->type->kind != TY_UNION)
        load(n->type);
      return;
    case ND_VA_ARG: {
      /* the fetch: once ap's gp/fp offsets and the two areas are up,
       * register-class args come out of reg_save_area[offset], the
       * rest follow the overflow area. only the class that was used
       * advances, and the advancement is written straight back into
       * the va_list object in %rcx */
      gen_expr(n->lhs);
      fprintf(out, "  mov %%rax, %%rcx\n");
      Type *tt = n->targ;
      int s = type_size(tt);
      int lbl1 = labeln++;
      int lbl2 = labeln++;
      if (tt->kind == TY_FLOAT || tt->kind == TY_DOUBLE) {
        fprintf(out, "  mov 4(%%rcx), %%eax\n");
        fprintf(out, "  cmp $176, %%eax\n");
        fprintf(out, "  jge .Lva%d\n", lbl1);
        fprintf(out, "  mov 16(%%rcx), %%rdi\n");
        fprintf(out, "  add %%rax, %%rdi\n");
        if (tt->kind == TY_FLOAT)
          fprintf(out, "  movss (%%rdi), %%xmm0\n");
        else
          fprintf(out, "  movsd (%%rdi), %%xmm0\n");
        fprintf(out, "  addl $16, 4(%%rcx)\n");
        fprintf(out, "  jmp .Lva%d\n", lbl2);
        fprintf(out, ".Lva%d:\n", lbl1);
        fprintf(out, "  mov 8(%%rcx), %%rdi\n");
        if (tt->kind == TY_FLOAT)
          fprintf(out, "  movss (%%rdi), %%xmm0\n");
        else
          fprintf(out, "  movsd (%%rdi), %%xmm0\n");
        fprintf(out, "  addl $8, 8(%%rcx)\n");
      } else {
        fprintf(out, "  mov (%%rcx), %%eax\n");
        fprintf(out, "  cmp $48, %%eax\n");
        fprintf(out, "  jge .Lva%d\n", lbl1);
        fprintf(out, "  mov 16(%%rcx), %%rdi\n");
        fprintf(out, "  add %%rax, %%rdi\n");
        switch (s) {
          case 1:
            if (tt->is_unsigned)
              fprintf(out, "  movzbq (%%rdi), %%rax\n");
            else
              fprintf(out, "  movsbq (%%rdi), %%rax\n");
            break;
          case 2:
            if (tt->is_unsigned)
              fprintf(out, "  movzwq (%%rdi), %%rax\n");
            else
              fprintf(out, "  movswq (%%rdi), %%rax\n");
            break;
          case 4:
            if (tt->is_unsigned || tt->kind == TY_PTR)
              fprintf(out, "  movl (%%rdi), %%eax\n");
            else
              fprintf(out, "  movslq (%%rdi), %%rax\n");
            break;
          default:
            fprintf(out, "  mov (%%rdi), %%rax\n");
        }
        fprintf(out, "  addl $8, (%%rcx)\n");
        fprintf(out, "  jmp .Lva%d\n", lbl2);
        fprintf(out, ".Lva%d:\n", lbl1);
        fprintf(out, "  mov 8(%%rcx), %%rdi\n");
        switch (s) {
          case 1:
            if (tt->is_unsigned)
              fprintf(out, "  movzbq (%%rdi), %%rax\n");
            else
              fprintf(out, "  movsbq (%%rdi), %%rax\n");
            break;
          case 2:
            if (tt->is_unsigned)
              fprintf(out, "  movzwq (%%rdi), %%rax\n");
            else
              fprintf(out, "  movswq (%%rdi), %%rax\n");
            break;
          case 4:
            if (tt->is_unsigned || tt->kind == TY_PTR)
              fprintf(out, "  movl (%%rdi), %%eax\n");
            else
              fprintf(out, "  movslq (%%rdi), %%rax\n");
            break;
          default:
            fprintf(out, "  mov (%%rdi), %%rax\n");
        }
        fprintf(out, "  addl $8, 8(%%rcx)\n");
      }
      fprintf(out, ".Lva%d:\n", lbl2);
      return;
    }
    default:
      error("internal: bad expression node %d", n->kind);
  }
}

/* assign a label to a case and emit its compare against the switch
 * value sitting in %rax; *arg is the default's label, or -1 */
static void gen_case_label(Node *c, void *arg) {
  int *def = arg;
  c->label = labeln++;
  if (!c->lhs) {
    *def = c->label;
    return;
  }
  CVal lo = const_fold(c->lhs);
  if (!c->rhs) {
    fprintf(out, "  cmp $%d, %%rax\n", lo.val);
    fprintf(out, "  je .L%d\n", c->label);
    return;
  }
  /* a range needs a full boundary check: jump to the body only when
   * start <= value <= end, otherwise fall through to the next label */
  CVal hi = const_fold(c->rhs);
  int skip = labeln++;
  fprintf(out, "  cmp $%d, %%rax\n", lo.val);
  fprintf(out, "  jl .L%d\n", skip);
  fprintf(out, "  cmp $%d, %%rax\n", hi.val);
  fprintf(out, "  jg .L%d\n", skip);
  fprintf(out, "  jmp .L%d\n", c->label);
  fprintf(out, ".L%d:\n", skip);
}

/* label registry for one function: name -> jump label number.
 * gotos are emitted in a second pass over the body, so forward
 * references resolve without fixups (labels are numbered up front) */
static char *lbl_names[256];
static int lbl_nums[256];
static int lbl_cnt;

/* walk the statement tree collecting every label; the shapes mirror
 * walk_cases. a goto into a block is fine: all locals live in the
 * function frame, and C only forbids it for VLA storage */
static void collect_labels(Node *s) {
  for (; s; s = s->next) {
    if (s->kind == ND_LABEL) {
      for (int i = 0; i < lbl_cnt; i++)
        if (strcmp(lbl_names[i], s->name) == 0)
          error("redefinition of label '%s'", s->name);
      lbl_names[lbl_cnt] = s->name;
      lbl_nums[lbl_cnt] = labeln++;
      s->label = lbl_nums[lbl_cnt];
      lbl_cnt++;
      if (s->body)
        collect_labels(s->body);
      continue;
    }
    switch (s->kind) {
      case ND_BLOCK:
        collect_labels(s->body);
        break;
      case ND_IF:
        collect_labels(s->then);
        if (s->els)
          collect_labels(s->els);
        break;
      case ND_WHILE:
      case ND_DO_WHILE:
      case ND_FOR:
        collect_labels(s->then);
        break;
      case ND_SWITCH:
        collect_labels(s->body);
        break;
      case ND_CASE:
        if (s->body)
          collect_labels(s->body);
        break;
      default:
        break;
    }
  }
}

/* one store per flattened leaf of a brace initializer; the target
 * is n->var in the current frame. shared by local ND_DECL and the
 * hidden object of a compound literal */
static void gen_init_stores(Node *n) {
  for (int i = 0; i < n->init_n; i++) {
    Init *it = &n->inits[i];
    if (it->expr) {
      gen_expr(it->expr);
      fprintf(out, "  push %%rax\n");
      fprintf(out, "  lea %d(%%rbp), %%rdi\n",
              n->var->offset + it->offset);
      fprintf(out, "  pop %%rax\n");
    } else {
      /* zero leaf */
      if (it->ty->kind == TY_DOUBLE)
        emit_const(0.0);
      else if (it->ty->kind == TY_FLOAT)
        emit_constf(0.0f);
      else
        fprintf(out, "  mov $0, %%rax\n");
      fprintf(out, "  lea %d(%%rbp), %%rdi\n",
              n->var->offset + it->offset);
    }
    store(it->ty);
  }
}

static void gen_stmt(Node *n) {
  switch (n->kind) {
    case ND_BLOCK:
      for (Node *s = n->body; s; s = s->next)
        gen_stmt(s);
      return;
    case ND_DECL:
      /* a VLA gets its storage right here, under the current stack
       * pointer: sub rsp, align16(size), then park the base in the
       * variable's 8-byte slot. the size is re-evaluated (its
       * variables may have changed since the declaration); and since
       * it is 16-aligned, rsp stays 16-aligned for any call that
       * follows. the epilogue discards the space by resetting rsp */
      if (type_is_vla(n->type)) {
        /* capture the size as the C standard's gcc does: sizeof on
         * the declared variable sees the size it had here, even if
         * the dimension variables change afterwards */
        gen_expr(n->vla_sz);
        fprintf(out, "  mov %%rax, %d(%%rbp)\n", n->var->size_off);
        fprintf(out, "  add $15, %%rax\n");
        fprintf(out, "  and $-16, %%rax\n");
        fprintf(out, "  sub %%rax, %%rsp\n");
        fprintf(out, "  mov %%rsp, %d(%%rbp)\n", n->var->offset);
        return;
      }
      /* C says uninitialized locals are garbage, so only the init
       * produces code; a static's init already landed in .data/.bss */
      if (n->init && !n->is_static) {
        if (n->inits) {
          gen_init_stores(n);
          return;
        }
        gen_expr(n->init);
        if (n->type->kind == TY_STRUCT || n->type->kind == TY_UNION) {
          /* struct init is a memcpy; the init expression already
           * evaluated to its address */
          fprintf(out, "  mov %%rax, %%rsi\n");
          fprintf(out, "  lea %d(%%rbp), %%rdi\n", n->var->offset);
          fprintf(out, "  mov $%d, %%rdx\n", n->type->size);
          fprintf(out, "  call memcpy\n");
        } else {
          fprintf(out, "  push %%rax\n");
          fprintf(out, "  lea %d(%%rbp), %%rdi\n", n->var->offset);
          fprintf(out, "  pop %%rax\n");
          store(n->type);
        }
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
    case ND_SWITCH: {
      /* compare the value in %rax against every case, jumping to the
       * matching body, the default, or the end of the switch */
      gen_expr(n->cond);
      int end = labeln++;
      int def = -1;
      walk_cases(n->body, gen_case_label, &def);
      if (def >= 0)
        fprintf(out, "  jmp .L%d\n", def);
      else
        fprintf(out, "  jmp .L%d\n", end);

      brk_labels[brk_n++] = end;
      gen_stmt(n->body);
      brk_n--;
      fprintf(out, ".L%d:\n", end);
      return;
    }
    case ND_LABEL:
      fprintf(out, ".L%d:\n", n->label);
      gen_stmt(n->body);
      return;
    case ND_GOTO: {
      int target = -1;
      for (int i = 0; i < lbl_cnt; i++)
        if (strcmp(lbl_names[i], n->name) == 0)
          target = lbl_nums[i];
      if (target < 0)
        error("use of undefined label '%s'", n->name);
      fprintf(out, "  jmp .L%d\n", target);
      return;
    }
    case ND_CASE:
      /* the label was assigned when the compare chain was emitted */
      fprintf(out, ".L%d:\n", n->label);
      gen_stmt(n->body);
      return;
    case ND_RETURN:
      if (n->lhs) {
        gen_expr(n->lhs);
        if (n->lhs->type->kind == TY_STRUCT || n->lhs->type->kind == TY_UNION) {
          /* copy into the hidden return buffer; its address rides in
           * rbp-relative memory, so no alignment juggling needed here */
          fprintf(out, "  mov %%rax, %%rsi\n");
          fprintf(out, "  mov %d(%%rbp), %%rdi\n", n->var->offset);
          fprintf(out, "  mov $%d, %%rdx\n", n->lhs->type->size);
          fprintf(out, "  call memcpy\n");
        } else if (cur_fn_ret->kind == TY_CHAR || cur_fn_ret->kind == TY_SHORT ||
             cur_fn_ret->kind == TY_INT) {
          /* a char/32-bit return is often produced by a 32-bit op in
           * %eax, leaving garbage in the upper 32 bits of %rax; rebuild
           * the full-width int so callers comparing 64-bit see -7, not
           * 249, and 0xdeadbeef stays 0xdeadbeef */
          if (cur_fn_ret->is_unsigned)
            fprintf(out, "  movl %%eax, %%eax\n");
          else
            fprintf(out, "  movslq %%eax, %%rax\n");
        }
      }
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

static void emit_data_align(Node *n) {
  /* an object over-aligned beyond its natural storage size needs an
   * explicit .balign (8 is derived anyway from the vector types the
   * backend already emits); a power of two */
  int a = type_align(n->type);
  if (n->align > a)
    a = n->align;
  if (a > 8)
    fprintf(out, "  .balign %d\n", a);
}

static void gen_data(Node *n) {
  /* storage class: static loses the .globl export, extern emits no
   * storage at all (the symbol is expected elsewhere at link time) */
  if (n->is_extern)
    return;
  char *sym = n->var ? (n->var->symname ? n->var->symname
                                        : n->var->name) : n->name;
  int exported = !n->is_static;
  if (n->inits) {
    /* brace initializer: one directive per leaf, .zero for the gaps
     * (struct members can have padding between them) and the tail */
    section(".data");
    if (exported)
      fprintf(out, "  .globl %s\n", sym);
    emit_data_align(n);
    fprintf(out, "%s:\n", sym);
    /* out-of-order designators can leave several leaves on one
     * offset; each offset keeps its last (winning) leaf, and the
     * leaves emit in offset order */
    int merges = 0;
    int *offs = xmalloc(sizeof(int) * n->init_n);
    int *last = xmalloc(sizeof(int) * n->init_n);
    for (int i = 0; i < n->init_n; i++) {
      int j;
      for (j = 0; j < merges; j++)
        if (offs[j] == n->inits[i].offset)
          break;
      if (j == merges) {
        offs[merges] = n->inits[i].offset;
        last[merges] = i;
        merges++;
      } else {
        last[j] = i;
      }
    }
    int off = 0;
    for (int k = 0; k < merges; k++) {
      int best = -1;
      for (int j = 0; j < merges; j++)
        if (offs[j] >= 0 && (best < 0 || offs[j] < offs[best]))
          best = j;
      Init *it = &n->inits[last[best]];
      offs[best] = -1;
      /* emit_string hops to .rodata and back to .text; the fields
       * themselves must stay in .data */
      section(".data");
      if (it->offset > off)
        fprintf(out, "  .zero %d\n", it->offset - off);
      if (!it->expr) {
        fprintf(out, "  .zero %d\n", it->ty->size);
      } else if (it->ty->kind == TY_DOUBLE) {
        CVal v = const_fold(it->expr);
        double d = v.is_float ? v.fval : (double)v.val;
        unsigned long long bits;
        memcpy(&bits, &d, 8);
        fprintf(out, "  .quad 0x%llx\n", bits);
      } else if (it->ty->kind == TY_FLOAT) {
        CVal v = const_fold(it->expr);
        float f = v.is_float ? (float)v.fval : (float)v.val;
        unsigned bits;
        memcpy(&bits, &f, 4);
        fprintf(out, "  .long 0x%x\n", bits);
      } else if (it->expr->kind == ND_STR) {
        /* a pointer slot fed by a string literal: the address */
        emit_string(it->expr);
        section(".data");
        fprintf(out, "  .quad %s\n", it->expr->var->name);
      } else if (it->expr->kind == ND_UNARY && it->expr->op == '&') {
        /* the address of a global is a link-time constant */
        Obj *tgt = it->expr->lhs->var;
        section(".data");
        fprintf(out, "  .quad %s\n",
                tgt->symname ? tgt->symname : tgt->name);
      } else {
        CVal v = const_fold(it->expr);
        int ival = v.is_float ? (int)v.fval : v.val;
        if (it->ty->is_bool)
          ival = ival != 0;
        fprintf(out, "  .%s %d\n",
                it->ty->size == 1 ? "byte" :
                it->ty->size == 2 ? "short" :
                it->ty->size == 4 ? "long" : "quad",
                ival);
      }
      off = it->offset + it->ty->size;
    }
    if (off < type_size(n->type)) {
      section(".data");
      fprintf(out, "  .zero %d\n", type_size(n->type) - off);
    }
    return;
  }
  if (n->init->kind == ND_STR) {
    /* the label must exist before the .quad references it */
    emit_string(n->init);
    section(".data");
    if (exported)
      fprintf(out, "  .globl %s\n", sym);
    emit_data_align(n);
    fprintf(out, "%s:\n", sym);
    fprintf(out, "  .quad %s\n", n->init->var->name);
    return;
  }
  if (n->init->kind == ND_UNARY && n->init->op == '&') {
    /* the address of a global is a link-time constant */
    section(".data");
    if (exported)
      fprintf(out, "  .globl %s\n", sym);
    emit_data_align(n);
    fprintf(out, "%s:\n", sym);
    Obj *tgt = n->init->lhs->var;
    fprintf(out, "  .quad %s\n", tgt->symname ? tgt->symname : tgt->name);
    return;
  }
  CVal v = const_fold(n->init);
  section(".data");
  if (exported)
    fprintf(out, "  .globl %s\n", sym);
  emit_data_align(n);
  fprintf(out, "%s:\n", sym);
  if (n->type->kind == TY_DOUBLE) {
    double d = v.is_float ? v.fval : (double)v.val;
    unsigned long long bits;
    memcpy(&bits, &d, 8);
    fprintf(out, "  .quad 0x%llx\n", bits);
    return;
  }
  if (n->type->kind == TY_FLOAT) {
    float f = v.is_float ? (float)v.fval : (float)v.val;
    unsigned bits;
    memcpy(&bits, &f, 4);
    fprintf(out, "  .long 0x%x\n", bits);
    return;
  }
  int ival = v.is_float ? (int)v.fval : v.val;
  if (n->type->is_bool)
    ival = ival != 0;
  fprintf(out, "  .%s %d\n",
          n->type->size == 1 ? "byte" :
          n->type->size == 2 ? "short" :
          n->type->size == 4 ? "long" : "quad",
          ival);
}

static void gen_func(Node *n) {
  cur_fn_ret = n->type->ret;
  section(".text");
  if (!n->is_static)
    fprintf(out, "  .globl %s\n", n->name);
  fprintf(out, "%s:\n", n->name);
  fprintf(out, "  push %%rbp\n");
  fprintf(out, "  mov %%rsp, %%rbp\n");

  int frame = n->var->frame;
  if (frame > 0)
    fprintf(out, "  sub $%d, %%rsp\n", frame);

  /* variadic prologue: park the incoming argument registers in the
   * 176-byte ~va save area before the param spill below runs, which
   * may call memcpy and clobber every one of them. ints at 8-byte
   * slots from the base, the xmm regs in 16-byte slots after them */
  if (n->type->is_variadic) {
    static char *vargreg[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    for (int i = 0; i < 6; i++)
      fprintf(out, "  mov %s, %d(%%rbp)\n", vargreg[i], n->val + i * 8);
    for (int i = 0; i < 8; i++)
      fprintf(out, "  movsd %%xmm%d, %d(%%rbp)\n", i, n->val + 48 + i * 16);
  }

  /* spill the SysV registers into the param slots. ints arrive in
   * rdi..r9, doubles in xmm0..7, each class counted independently;
   * whatever overflows its budget sits on the stack in argument
   * order, at 16(%rbp) and up. float parameters arrive as doubles
   * (see gen_call) and are narrowed on the way in */
  static char *argreg[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
  int idx, sse, stk;

  /* pass 1: park every register-sourced parameter in its own slot,
   * before the struct-param memcpys clobber all the argument
   * registers (stack-sourced ones already live in memory, which
   * memcpy never touches) */
  idx = 0;
  sse = 0;
  for (Node *p = n->type->params; p; p = p->next) {
    Type *pt = p->var->type;
    if (pt->kind == TY_FLOAT || pt->kind == TY_DOUBLE) {
      if (sse < 8)
        fprintf(out, "  movsd %%xmm%d, %d(%%rbp)\n", sse,
                p->var->offset);
      sse++;
    } else {
      if (idx < 6)
        fprintf(out, "  mov %s, %d(%%rbp)\n", argreg[idx],
                p->var->offset);
      idx++;
    }
  }

  idx = 0;
  sse = 0;
  stk = 0;
  Obj *sret = NULL;
  for (Node *p = n->type->params; p; p = p->next) {
    if (p->var->name && strcmp(p->var->name, "~ret") == 0)
      sret = p->var;
    if (p->var->type->kind == TY_FLOAT) {
      if (sse < 8)
        fprintf(out, "  movsd %d(%%rbp), %%xmm0\n"
                     "  cvtsd2ss %%xmm0, %%xmm0\n"
                     "  movss %%xmm0, %d(%%rbp)\n",
                p->var->offset, p->var->offset);
      else
        fprintf(out, "  movsd %d(%%rbp), %%xmm0\n"
                     "  cvtsd2ss %%xmm0, %%xmm0\n"
                     "  movss %%xmm0, %d(%%rbp)\n",
                16 + stk * 8, p->var->offset);
      sse++;
    } else if (p->var->type->kind == TY_DOUBLE) {
      if (sse < 8)
        fprintf(out, "  movsd %d(%%rbp), %%xmm0\n"
                     "  movsd %%xmm0, %d(%%rbp)\n",
                p->var->offset, p->var->offset);
      else
        fprintf(out, "  movsd %d(%%rbp), %%xmm0\n"
                     "  movsd %%xmm0, %d(%%rbp)\n",
                16 + stk * 8, p->var->offset);
      sse++;
    } else if (p->var->type->kind == TY_STRUCT ||
             p->var->type->kind == TY_UNION) {
      /* struct parameters arrive as addresses (see gen_call); copy
       * the object into its slot. counts against the int budget. the
       * address is taken from the slot parked in pass 1 (stack-sourced
       * ones are already memory, memcpy never touches them) */
      if (idx < 6)
        fprintf(out, "  mov %d(%%rbp), %%rsi\n", p->var->offset);
      else
        fprintf(out, "  mov %d(%%rbp), %%rsi\n", 16 + stk * 8);
      fprintf(out, "  lea %d(%%rbp), %%rdi\n", p->var->offset);
      fprintf(out, "  mov $%d, %%rdx\n", p->var->type->size);
      fprintf(out, "  call memcpy\n");
      idx++;
    } else {
      if (idx < 6)
        fprintf(out, "  mov %d(%%rbp), %%rax\n"
                     "  mov %%rax, %d(%%rbp)\n",
                p->var->offset, p->var->offset);
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
  lbl_cnt = 0;
  collect_labels(n->body);
  ret_label = labeln++;
  gen_stmt(n->body);

  fprintf(out, ".L.ret%d:\n", ret_label);
  if (sret)
    fprintf(out, "  mov %d(%%rbp), %%rax\n", sret->offset);
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
      if (n->is_extern) {
        /* nothing to emit; the symbol lives in another unit */
      } else if (n->init) {
        gen_data(n);
      } else if (n->type->kind != TY_ARRAY || type_size(n->type) > 0) {
        section(".bss");
        if (!n->is_static)
          fprintf(out, "  .globl %s\n", n->name);
        emit_data_align(n);
        fprintf(out, "%s:\n", n->name);
        fprintf(out, "  .zero %d\n", type_size(n->type));
      }
    }
  }

  for (int i = 0; i < static_decls_n; i++) {
    Node *s = static_decls[i];
    if (s->init)
      gen_data(s);
    else {
      section(".bss");
      emit_data_align(s);
      fprintf(out, "%s:\n", s->var->symname);
      fprintf(out, "  .zero %d\n", type_size(s->type));
    }
  }

  fclose(out);
}