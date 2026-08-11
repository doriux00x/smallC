#include "parser.h"
#include "token.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Token *tok;

/* loops we're nested in, so break/continue can be validated */
static int nloop;

static Token *consume(TokenKind k) {
  if (tok->kind != k)
    return NULL;
  Token *t = tok;
  tok = tok->next;
  return t;
}

static int at(TokenKind k) {
  return tok->kind == k;
}

static Token *expect(TokenKind k, char *what) {
  if (tok->kind != k)
    error_at(tok->loc, "expected %s", what);
  Token *t = tok;
  tok = tok->next;
  return t;
}

static Token *expect_ident(char *what) {
  return expect(TK_IDENT, what);
}

static int is_punct(char *op) {
  return tok->kind == TK_PUNCT &&
         tok->len == (int)strlen(op) &&
         memcmp(tok->loc, op, tok->len) == 0;
}

static int is_punct_next(char *op) {
  Token *t = tok->next;
  if (!t)
    return 0;
  return t->kind == TK_PUNCT &&
         t->len == (int)strlen(op) &&
         memcmp(t->loc, op, t->len) == 0;
}

static int consume_punct(char *op) {
  if (!is_punct(op))
    return 0;
  tok = tok->next;
  return 1;
}

static void expect_punct(char *op) {
  if (!is_punct(op))
    error_at(tok->loc, "expected '%s'", op);
  tok = tok->next;
}

/* -------- types -------- */

/* struct tags live in their own namespace, like C says */
typedef struct Tag Tag;
struct Tag {
  Tag *next;
  char *name;
  Type *type;
};

static Tag *tags;

static Type *parse_typespec(void);
static Type *declarator(Type *base, char **name);

static Type *find_tag(char *name) {
  for (Tag *t = tags; t; t = t->next)
    if (strcmp(t->name, name) == 0)
      return t->type;
  return NULL;
}

static void register_tag(char *name, Type *type) {
  Tag *t = xmalloc(sizeof(Tag));
  t->name = name;
  t->type = type;
  t->next = tags;
  tags = t;
}

static Member *parse_struct_members(void) {
  Member head = {0};
  Member **link = &head.next;

  while (!is_punct("}")) {
    if (tok->kind == TK_EOF)
      error_at(tok->loc, "unexpected EOF inside struct definition");
    Type *base = parse_typespec();
    for (;;) {
      char *name;
      Type *mt = declarator(base, &name);
      if (!name)
        error_at(tok->loc, "struct members must be named");
      Member *m = xmalloc(sizeof(Member));
      m->next = NULL;
      m->name = name;
      m->type = mt;
      *link = m;
      link = &m->next;
      if (!consume_punct(","))
        break;
    }
    expect_punct(";");
  }
  consume_punct("}");
  return head.next;
}

static int is_typespec_start(Token *t) {
  return t->kind == TK_VOID || t->kind == TK_CHAR || t->kind == TK_SHORT ||
         t->kind == TK_INT || t->kind == TK_LONG || t->kind == TK_SIGNED ||
         t->kind == TK_UNSIGNED || t->kind == TK_FLOAT ||
         t->kind == TK_DOUBLE || t->kind == TK_STRUCT;
}

/* any run of type keywords: "unsigned long long" etc. */
static Type *parse_typespec(void) {
  int is_unsigned = 0;
  int longs = 0;
  Type *t = NULL;

  for (;;) {
    if (consume(TK_SIGNED))    { is_unsigned = 0; continue; }
    if (consume(TK_UNSIGNED))  { is_unsigned = 1; continue; }
    if (consume(TK_LONG))      { longs++;         continue; }
    if (consume(TK_VOID))      { t = type_new(TY_VOID);   continue; }
    if (consume(TK_CHAR))      { t = type_new(TY_CHAR);   continue; }
    if (consume(TK_SHORT))     { t = type_new(TY_SHORT);  continue; }
    if (consume(TK_INT))       { t = type_new(TY_INT);    continue; }
    if (consume(TK_FLOAT))     { t = type_new(TY_FLOAT);  continue; }
    if (consume(TK_DOUBLE))    { t = type_new(TY_DOUBLE); continue; }
    if (consume(TK_STRUCT)) {
      char *tag = NULL;
      if (at(TK_IDENT))
        tag = expect(TK_IDENT, "struct tag")->name;
      if (consume_punct("{")) {
        /* the tag goes in before the members, so the body can
         * reference itself (struct Node *next) */
        Type *st = struct_type();
        if (tag)
          register_tag(tag, st);
        st->members = parse_struct_members();
        if (!st->members)
          error_at(tok->loc, "empty struct");
        layout_struct(st);
        t = st;
        continue;
      }
      if (!tag)
        error_at(tok->loc, "expected struct tag");
      t = find_tag(tag);
      if (!t)
        error_at(tok->loc, "unknown struct '%s'", tag);
      continue;
    }
    break;
  }

  if (!t)
    t = type_new(longs ? TY_LONG : TY_INT);
  if (longs >= 2)
    t->is_longlong = 1;
  t->is_unsigned = is_unsigned;
  return t;
}

/* declarator and its helpers live in the declarations section */
static Type *declarator(Type *base, char **name);
static Type *suffix_loop(Type *t);

/* -------- operators -------- */

static int to_op(void) {
  if (tok->kind != TK_PUNCT)
    return 0;
  if (tok->len == 1)
    return tok->loc[0];

  typedef struct { char *s; int op; } OpMap;
  static OpMap ops[] = {
    {"<<", OP_SHL}, {">>", OP_SHR},
    {"==", OP_EQ}, {"!=", OP_NE}, {"<=", OP_LE}, {">=", OP_GE},
    {"&&", OP_LOGAND}, {"||", OP_LOGOR},
    {"+=", OP_ADD_ASSIGN}, {"-=", OP_SUB_ASSIGN}, {"*=", OP_MUL_ASSIGN},
    {"/=", OP_DIV_ASSIGN}, {"%=", OP_MOD_ASSIGN},
    {"<<=", OP_SHL_ASSIGN}, {">>=", OP_SHR_ASSIGN},
    {"&=", OP_AND_ASSIGN}, {"|=", OP_OR_ASSIGN}, {"^=", OP_XOR_ASSIGN},
    {"++", OP_INC}, {"--", OP_DEC},
  };
  for (int i = 0; i < (int)ARRAY_LEN(ops); i++)
    if (tok->len == (int)strlen(ops[i].s) &&
        memcmp(tok->loc, ops[i].s, tok->len) == 0)
      return ops[i].op;
  return 0;
}

/* returns the current token's op code if it's in the list, else 0 */
static int check_op(int *ops) {
  int op = to_op();
  if (!op)
    return 0;
  for (int i = 0; ops[i]; i++)
    if (ops[i] == op)
      return op;
  return 0;
}

static int is_assign_op(int op) {
  return op == '=' || (op >= OP_ADD_ASSIGN && op <= OP_XOR_ASSIGN);
}

/* -------- node constructors -------- */

static Node *node_new(NodeKind k) {
  Node *n = xmalloc(sizeof(Node));
  n->kind = k;
  return n;
}

static Node *new_binary(int op, Node *lhs, Node *rhs) {
  Node *n = node_new(ND_BIN);
  n->op = op;
  n->lhs = lhs;
  n->rhs = rhs;
  return n;
}

static Node *new_unary(int op, Node *lhs) {
  Node *n = node_new(ND_UNARY);
  n->op = op;
  n->lhs = lhs;
  return n;
}

/* -------- expressions, lowest precedence first -------- */

static Node *parse_assign(void);
static Node *parse_cond(void);
static Node *parse_logor(void);
static Node *parse_logand(void);
static Node *parse_bitor(void);
static Node *parse_bitxor(void);
static Node *parse_bitand(void);
static Node *parse_equality(void);
static Node *parse_relational(void);
static Node *parse_shift(void);
static Node *parse_add(void);
static Node *parse_mul(void);
static Node *parse_unary(void);
static Node *parse_postfix(void);
static Node *parse_primary(void);

static Node *parse_expr(void) {
  return parse_assign();
}

static Node *parse_assign(void) {
  Node *node = parse_cond();

  int op = to_op();
  if (!is_assign_op(op))
    return node;
  tok = tok->next;

  Node *n = node_new(ND_ASSIGN);
  n->op = op;
  n->lhs = node;
  n->rhs = parse_assign();   /* right assoc */
  return n;
}

static Node *parse_cond(void) {
  Node *cond = parse_logor();

  if (!consume_punct("?"))
    return cond;

  Node *then = parse_expr();
  expect_punct(":");
  Node *els = parse_cond();  /* right assoc */

  Node *n = node_new(ND_COND);
  n->cond = cond;
  n->then = then;
  n->els = els;
  return n;
}

static int logor_ops[] = {OP_LOGOR, 0};
static int logand_ops[] = {OP_LOGAND, 0};
static int bitor_ops[] = {'|', 0};
static int bitxor_ops[] = {'^', 0};
static int bitand_ops[] = {'&', 0};
static int eq_ops[] = {OP_EQ, OP_NE, 0};
static int rel_ops[] = {'<', '>', OP_LE, OP_GE, 0};
static int shift_ops[] = {OP_SHL, OP_SHR, 0};
static int add_ops[] = {'+', '-', 0};
static int mul_ops[] = {'*', '/', '%', 0};

static Node *parse_logor(void) {
  Node *node = parse_logand();
  for (;;) {
    int op = check_op(logor_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_logand());
  }
}

static Node *parse_logand(void) {
  Node *node = parse_bitor();
  for (;;) {
    int op = check_op(logand_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_bitor());
  }
}

static Node *parse_bitor(void) {
  Node *node = parse_bitxor();
  for (;;) {
    int op = check_op(bitor_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_bitxor());
  }
}

static Node *parse_bitxor(void) {
  Node *node = parse_bitand();
  for (;;) {
    int op = check_op(bitxor_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_bitand());
  }
}

static Node *parse_bitand(void) {
  Node *node = parse_equality();
  for (;;) {
    int op = check_op(bitand_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_equality());
  }
}

static Node *parse_equality(void) {
  Node *node = parse_relational();
  for (;;) {
    int op = check_op(eq_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_relational());
  }
}

static Node *parse_relational(void) {
  Node *node = parse_shift();
  for (;;) {
    int op = check_op(rel_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_shift());
  }
}

static Node *parse_shift(void) {
  Node *node = parse_add();
  for (;;) {
    int op = check_op(shift_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_add());
  }
}

static Node *parse_add(void) {
  Node *node = parse_mul();
  for (;;) {
    int op = check_op(add_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_mul());
  }
}

static Node *parse_mul(void) {
  Node *node = parse_unary();
  for (;;) {
    int op = check_op(mul_ops);
    if (!op)
      return node;
    tok = tok->next;
    node = new_binary(op, node, parse_unary());
  }
}

/* -------- unary and postfix -------- */

static Node *parse_unary(void) {
  if (consume_punct("+")) return new_unary('+', parse_unary());
  if (consume_punct("-")) return new_unary('-', parse_unary());
  if (consume_punct("!")) return new_unary('!', parse_unary());
  if (consume_punct("~")) return new_unary('~', parse_unary());
  if (consume_punct("*")) return new_unary('*', parse_unary());
  if (consume_punct("&")) return new_unary('&', parse_unary());

  /* (typename) cast; the lookahead mirrors sizeof: a type keyword
   * right after '(' can never start a parenthesized expression */
  if (is_punct("(") && tok->next && is_typespec_start(tok->next)) {
    tok = tok->next;
    char *dummy;
    Type *ty = declarator(parse_typespec(), &dummy);
    expect_punct(")");
    Node *n = node_new(ND_CAST);
    n->targ = ty;
    n->lhs = parse_unary();
    return n;
  }

  int op = to_op();
  if (op == OP_INC || op == OP_DEC) {
    tok = tok->next;
    Node *n = new_unary(op, parse_unary());
    n->is_prefix = 1;
    return n;
  }

  return parse_postfix();
}

static Node *parse_postfix(void) {
  Node *node = parse_primary();

  for (;;) {
    if (consume_punct("(")) {
      Node *call = node_new(ND_CALL);
      call->lhs = node;

      Node head = {0};
      Node **link = &head.next;
      if (!consume_punct(")")) {
        for (;;) {
          Node *arg = parse_assign();
          *link = arg;
          link = &arg->next;
          if (consume_punct(")"))
            break;
          expect_punct(",");
        }
      }
      call->args = head.next;
      node = call;
      continue;
    }

    if (consume_punct("[")) {
      Node *idx = node_new(ND_INDEX);
      idx->lhs = node;
      idx->rhs = parse_expr();
      expect_punct("]");
      node = idx;
      continue;
    }

    if (consume_punct(".")) {
      Token *ident = expect_ident("member name");
      Node *m = node_new(ND_MEMBER);
      m->lhs = node;
      m->name = ident->name;
      m->is_pntr = 0;
      node = m;
      continue;
    }

    if (consume_punct("->")) {
      Token *ident = expect_ident("member name");
      Node *m = node_new(ND_MEMBER);
      m->lhs = node;
      m->name = ident->name;
      m->is_pntr = 1;
      node = m;
      continue;
    }

    int op = to_op();
    if (op == OP_INC || op == OP_DEC) {
      tok = tok->next;
      node = new_unary(op, node);
      node->is_prefix = 0;
      continue;
    }

    return node;
  }
}

static Node *parse_primary(void) {
  /* no typedefs yet, so an ident after '(' can never be a type name */
  if (at(TK_SIZEOF)) {
    tok = tok->next;
    Type *ty = NULL;
    if (is_punct("(") && tok->next && is_typespec_start(tok->next)) {
      tok = tok->next;
      char *dummy;
      ty = declarator(parse_typespec(), &dummy);
      expect_punct(")");
    }

    Node *n = node_new(ND_SIZEOF);
    if (ty)
      n->targ = ty;
    else
      n->lhs = parse_unary();
    return n;
  }

  if (consume_punct("(")) {
    Node *node = parse_expr();
    expect_punct(")");
    return node;
  }

  Token *t;
  if ((t = consume(TK_NUM))) {
    Node *n = node_new(ND_NUM);
    if (t->is_float) {
      n->is_float = 1;
      n->is_f = t->is_f;
      n->fval = t->fval;
    } else {
      n->val = t->val;
    }
    return n;
  }

  if ((t = consume(TK_STR))) {
    Node *n = node_new(ND_STR);
    n->str = t->str;
    n->str_len = t->str_len;
    return n;
  }

  if ((t = consume(TK_IDENT))) {
    Node *n = node_new(ND_VAR);
    n->name = t->name;
    /* FIXME: no symbol table yet, var nodes resolve at codegen time */
    return n;
  }

  error_at(tok->loc, "expected expression");
}

/* -------- statements -------- */

static Node *parse_declaration(void);
static Node *parse_stmt(void);
static Node *parse_block(void);

static Node *parse_block(void) {
  /* '{' already consumed */
  Node head = {0};
  Node **link = &head.next;

  while (!is_punct("}")) {
    if (tok->kind == TK_EOF)
      error_at(tok->loc, "unexpected EOF, missing '}'");
    Node *s = parse_stmt();
    while (s) {
      *link = s;
      link = &s->next;
      s = s->next;
    }
  }
  tok = tok->next;   /* '}' */

  Node *n = node_new(ND_BLOCK);
  n->body = head.next;
  return n;
}

static Node *parse_stmt(void) {
  if (consume_punct("{"))
    return parse_block();

  if (consume(TK_RETURN)) {
    Node *n = node_new(ND_RETURN);
    if (!is_punct(";"))
      n->lhs = parse_expr();
    expect_punct(";");
    return n;
  }

  if (consume(TK_IF)) {
    expect_punct("(");
    Node *cond = parse_expr();
    expect_punct(")");

    Node *then = parse_stmt();
    Node *els = NULL;
    if (consume(TK_ELSE))
      els = parse_stmt();

    Node *n = node_new(ND_IF);
    n->cond = cond;
    n->then = then;
    n->els = els;
    return n;
  }

  if (consume(TK_WHILE)) {
    expect_punct("(");
    Node *cond = parse_expr();
    expect_punct(")");

    Node *n = node_new(ND_WHILE);
    n->cond = cond;
    nloop++;
    n->then = parse_stmt();
    nloop--;
    return n;
  }

  if (consume(TK_DO)) {
    Node *n = node_new(ND_DO_WHILE);
    nloop++;
    n->then = parse_stmt();
    nloop--;
    expect(TK_WHILE, "'while' after do block");
    expect_punct("(");
    n->cond = parse_expr();
    expect_punct(")");
    expect_punct(";");
    return n;
  }

  if (consume(TK_FOR)) {
    expect_punct("(");

    Node *init = NULL;
    if (!consume_punct(";")) {
      if (is_typespec_start(tok))
        init = parse_declaration();   /* consumes its own ';' */
      else {
        Node *e = node_new(ND_EXPR_STMT);
        e->lhs = parse_expr();
        expect_punct(";");
        init = e;
      }
    }

    Node *cond = NULL;
    if (!is_punct(";"))
      cond = parse_expr();
    expect_punct(";");

    Node *inc = NULL;
    if (!is_punct(")"))
      inc = parse_expr();
    expect_punct(")");

    Node *n = node_new(ND_FOR);
    n->init = init;
    n->cond = cond;
    n->inc = inc;
    nloop++;
    n->then = parse_stmt();
    nloop--;
    return n;
  }

  if (consume(TK_BREAK)) {
    if (!nloop)
      error_at(tok->loc, "break outside of loop");
    expect_punct(";");
    return node_new(ND_BREAK);
  }

  if (consume(TK_CONTINUE)) {
    if (!nloop)
      error_at(tok->loc, "continue outside of loop");
    expect_punct(";");
    return node_new(ND_CONTINUE);
  }

  if (is_typespec_start(tok)) {
    Node *s = parse_declaration();
    for (Node *m = s; m; m = m->next)
      if (m->kind == ND_FUNC && m->body)
        /* the body's parse already swallowed the enclosing block */
        error("function '%s' defined inside a block, unsupported", m->name);
    return s;
  }

  /* expression statement; a bare ';' also lands here */
  Node *e = node_new(ND_EXPR_STMT);
  if (!consume_punct(";"))
    e->lhs = parse_expr();
  expect_punct(";");
  return e;
}

/* -------- declarations -------- */

/* "(" params ")" becomes a function type with the given return type */
static Type *parse_params(Type *ret) {
  expect_punct("(");
  Type *ft = func_type(ret);

  Node head = {0};
  Node **link = &head.next;

  if (!consume_punct(")")) {
    /* "(void)" alone means no params */
    if (at(TK_VOID) && is_punct_next(")")) {
      tok = tok->next;
      expect_punct(")");
    } else {
      for (;;) {
        Type *pt = parse_typespec();
        char *pname = NULL;
        pt = declarator(pt, &pname);   /* abstract declarators allowed */

        Node *pn = node_new(ND_DECL);
        pn->name = pname;
        pn->type = pt;
        *link = pn;
        link = &pn->next;

        if (consume_punct(")"))
          break;
        expect_punct(",");
      }
    }
  }

  ft->params = head.next;
  return ft;
}

/* array and parameter suffixes; "f[3](int)" is array of func */
static Type *suffix_loop(Type *t) {
  for (;;) {
    if (consume_punct("[")) {
      int len = 0;
      if (at(TK_NUM)) {
        len = tok->val;
        tok = tok->next;
      }
      expect_punct("]");
      t = array_of(t, len);
      continue;
    }
    if (is_punct("("))
      t = parse_params(t);
    else
      return t;
  }
}

/* the full C declarator grammar, including "(*fp)(int)".
 * <name> comes back NULL for abstract declarators (params, sizeof).
 *
 * the parenthesized case is the fun part: the inner declarator is
 * parsed against a dummy type, so the group's chain (ptr/array) is
 * built by wrapping the dummy. anything after the group is a function
 * or array suffix which binds to the base + outer stars instead -
 * "int (*fp)(int)" means fp is a pointer to a function, not a function
 * returning a pointer. so the real type is stitched into the leaf of
 * the chain where the dummy sits */
static Type *declarator(Type *base, char **name) {
  Type *t = base;
  while (consume_punct("*"))
    t = ptr_to(t);

  if (consume_punct("(")) {
    Type dummy = {0};
    Type *inner = declarator(&dummy, name);
    expect_punct(")");

    t = suffix_loop(t);
    if (inner == &dummy) {
      inner = t;   /* "(fp)(int)": no wraps inside at all */
    } else {
      /* walk the inner chain to the leaf holding the dummy; functions
       * keep their "chain" in ret, everything else in base */
      Type *p = inner;
      for (;;) {
        Type **slot = (p->kind == TY_FUNC) ? &p->ret : &p->base;
        if (*slot == &dummy)
          break;
        p = *slot;
      }
      *((p->kind == TY_FUNC) ? &p->ret : &p->base) = t;
    }
    return inner;
  }

  *name = NULL;
  if (at(TK_IDENT))
    *name = expect_ident("identifier")->name;
  return suffix_loop(t);
}

/* a named declarator, wrapped into a node; functions may pick up a
 * body here */
static Node *parse_declarator(Type *base, Type **out) {
  char *name;
  Type *t = declarator(base, &name);
  *out = t;

  if (t->kind == TY_FUNC) {
    Node *n = node_new(ND_FUNC);
    n->name = name;
    n->type = t;
    if (consume_punct("{"))
      n->body = parse_block();
    return n;
  }

  Node *n = node_new(ND_DECL);
  n->name = name;
  n->type = t;

  if (consume_punct("=")) {
    if (is_punct("{"))
      error_at(tok->loc, "aggregate initializers not implemented yet");
    n->init = parse_assign();
  }
  return n;
}

static Node *parse_declaration(void) {
  Node *first = NULL;
  Node **link = &first;
  Type *base = parse_typespec();

  for (;;) {
    Type *t;
    Node *n = parse_declarator(base, &t);
    if (!n->name && n->kind == ND_DECL) {
      /* a type-only declaration ("struct point {...};") carries
       * no storage, just a tag definition */
      expect_punct(";");
      return NULL;
    }
    *link = n;
    link = &n->next;

    /* a function with a body ends the list; there is no trailing ';' */
    if (n->kind == ND_FUNC && n->body)
      return first;

    if (!consume_punct(","))
      break;
  }

  expect_punct(";");
  return first;
}

static Node *parse_program(void) {
  Node head = {0};
  Node **link = &head.next;

  while (tok->kind != TK_EOF) {
    if (consume_punct(";"))
      continue;   /* stray semicolon, legal at top level */

    Node *decls = parse_declaration();
    while (decls) {
      *link = decls;
      link = &decls->next;
      decls = decls->next;
    }
  }
  return head.next;
}

Node *parse(Token *t) {
  tok = t;
  return parse_program();
}