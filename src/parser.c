#include "parser.h"
#include "token.h"
#include "util.h"

#include "libc.h"

static Token *tok;
static Type *cur_fn;   /* the function whose body is being parsed */

/* loops we're nested in, so break/continue can be validated */
static int nloop;
/* switches we're nested in; case labels may only appear in the
 * switch body's own statement list, so depth must be exactly 1 */
static int nswitch;

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

/* the ident is a typedef-name; it is the name being declared, not a
 * type, when a declaration- or expression-ending punct follows it:
 * ";", "," or any operator ("typedef char T;", "int T = 5;",
 * "T += 2;" where T is a shadowing variable) */
static int typedef_ident_is_name(Token *t) {
  Token *n = t->next;
  if (!n || n->kind != TK_PUNCT)
    return 0;
  if (n->len == 1)
    return n->loc[0] == ';' || n->loc[0] == ',' || n->loc[0] == '=';
  return 1;
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

static Type *parse_typespec(int *alignas_ret);
static Type *declarator(Type *base, char **name);
static Node *parse_expr(void);
static Node *parse_assign(void);

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

/* typedef names also live in their own registry, like tags; they are
 * looked up at parse time because they change how declarations parse */
typedef struct Typedef Typedef;
struct Typedef {
  Typedef *next;
  char *name;
  Type *type;
};

static Typedef *typedefs;

static Type *find_typedef(char *name) {
  for (Typedef *t = typedefs; t; t = t->next)
    if (strcmp(t->name, name) == 0)
      return t->type;
  return NULL;
}

static void register_typedef(char *name, Type *type) {
  Typedef *t = xmalloc(sizeof(Typedef));
  t->name = name;
  t->type = type;
  t->next = typedefs;
  typedefs = t;
}

/* declared identifiers' types, for typeof(expr) at parse time. xmalloc
 * names shadow (newest wins), exactly like the typedef registry: the
 * compiler never type-checks against this table, it only answers
 * "what was the most recent declaration of this name's type?" */
typedef struct ParseVar ParseVar;
struct ParseVar {
  ParseVar *next;
  char *name;
  Type *type;
};

static ParseVar *parse_vars;

static Type *find_parse_var(char *name) {
  for (ParseVar *e = parse_vars; e; e = e->next)
    if (strcmp(e->name, name) == 0)
      return e->type;
  return NULL;
}

static void register_parse_var(char *name, Type *type) {
  ParseVar *e = xmalloc(sizeof(ParseVar));
  e->name = name;
  e->type = type;
  e->next = parse_vars;
  parse_vars = e;
}

/* the compile-time type of a just-parsed expression, computed at parse
 * time for typeof(expr). this mirrors what the resolve pass would do
 * with the full symbol side, but it only needs the *type*, so it walks
 * the fresh AST over the parse scope's registry */
static Type *infer_type(Node *n) {
  switch (n->kind) {
    case ND_NUM:
      if (n->is_float)
        return type_new(n->is_f ? TY_FLOAT : TY_DOUBLE);
      if (n->is_long)
        return type_new(TY_LONG);
      return type_new(TY_INT);
    case ND_STR:
      return array_of(type_new(TY_CHAR), n->str_len + 1);
    case ND_LABEL_ADDR:
      /* a label address is a void*, like gcc */
      return ptr_to(type_new(TY_VOID));
    case ND_VAR: {
      Type *t = find_parse_var(n->name);
      if (!t)
        error_at(tok->loc, "typeof operand '%s' has no known type", n->name);
      return t;
    }
    case ND_MEMBER: {
      Type *st = n->is_pntr ? infer_type(n->lhs)->base : infer_type(n->lhs);
      if (st->kind != TY_STRUCT && st->kind != TY_UNION)
        error("member access on a non-aggregate in typeof");
      for (Member *m = st->members; m; m = m->next)
        if (m->name && strcmp(m->name, n->name) == 0)
          return m->type;
      error("no member named '%s' in typeof operand", n->name);
    }
    case ND_INDEX: {
      Type *base = infer_type(n->lhs);
      if (base->kind == TY_ARRAY || base->kind == TY_PTR)
        return base->base;
      error("indexing a non-array in typeof");
    }
    case ND_UNARY:
      switch (n->op) {
        case '*': {
          Type *base = infer_type(n->lhs);
          if (base->kind == TY_PTR)
            return base->base;
          error("dereferencing a non-pointer in typeof");
        }
        case '&':
          return ptr_to(infer_type(n->lhs));
        case '!':
          return type_new(TY_INT);
        case '+':
        case '-':
          return infer_type(n->lhs);
        default:
          /* ~, ++, -- keep the operand's type (int after promotion) */
          return infer_type(n->lhs);
      }
    case ND_BIN:
      if (n->op == ',')
        return infer_type(n->rhs);
      {
        Type *l = infer_type(n->lhs);
        Type *r = infer_type(n->rhs);
        switch (n->op) {
          case OP_EQ: case OP_NE: case '<': case '>':
          case OP_LE: case OP_GE: case OP_LOGAND: case OP_LOGOR:
            return type_new(TY_INT);
          default:
            break;
        }
        if (l->kind == TY_DOUBLE || r->kind == TY_DOUBLE)
          return type_new(TY_DOUBLE);
        if (l->kind == TY_FLOAT || r->kind == TY_FLOAT)
          return type_new(TY_FLOAT);
        if (l->kind == TY_LONG || r->kind == TY_LONG)
          return type_new(TY_LONG);
        return type_new(TY_INT);
      }
    case ND_ASSIGN:
      return infer_type(n->lhs);
    case ND_COND: {
      Type *a = infer_type(n->then);
      Type *b = infer_type(n->els);
      if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE)
        return type_new(TY_DOUBLE);
      if (a->kind == TY_FLOAT || b->kind == TY_FLOAT)
        return type_new(TY_FLOAT);
      if (a->kind == TY_LONG || b->kind == TY_LONG)
        return type_new(TY_LONG);
      return type_new(TY_INT);
    }
    case ND_CALL: {
      Type *f = infer_type(n->lhs);
      if (f->kind == TY_FUNC)
        return f->ret;
      if (f->kind == TY_PTR && f->base->kind == TY_FUNC)
        return f->base->ret;
      error("calling a non-function in typeof");
    }
    case ND_CAST:
      return n->targ;
    case ND_COMP_LIT:
      return n->targ;
    case ND_SIZEOF:
    case ND_ALIGNOF: {
      /* sizeof/_Alignof yield size_t (unsigned long); this surfaces
       * in typeof where the operand's type is the question */
      Type *sz = type_new(TY_LONG);
      sz->is_unsigned = 1;
      return sz;
    }
    case ND_VA_ARG:
      return n->targ;
    case ND_STMT_EXPR:
      /* the value of a statement expression is its last expression */
      if (n->then)
        return infer_type(n->then);
      return type_new(TY_VOID);
    case ND_GENERIC: {
      /* the controlling expression is only examined for its type; the
       * chosen arm's type is the selection's type. duplicates are as
       * fatal here as they are at resolve time */
      Type *control = infer_type(n->cond);
      if (control->kind == TY_ARRAY || control->kind == TY_FUNC)
        control = ptr_to(control->base);
      Type *t = NULL, *deflt = NULL;
      for (Node *a = n->els; a; a = a->next) {
        if (!a->targ) {
          if (deflt)
            error("duplicate default in _Generic");
          deflt = infer_type(a->lhs);
          continue;
        }
        if (generic_match(control, a->targ, 1)) {
          if (t)
            error("duplicate match in _Generic");
          t = infer_type(a->lhs);
        }
      }
      if (!t)
        t = deflt;
      if (!t)
        error("no association matches the _Generic controlling type");
      return t;
    }
    default:
      error("unsupported expression in typeof");
  }
}

/* the stdarg machinery's va_list: a builtin typedef for a struct
 * whose layout matches the SysV ABI (two 4-byte offsets, then the
 * overflow and register save areas), so a va_list built here can be
 * handed to libc's vprintf */
static Type *builtin_va_list_type(void) {
  static Type *cached;
  if (cached)
    return cached;

  Type *st = struct_type();
  char *names[4] = { "gp_offset", "fp_offset",
                     "overflow_arg_area", "reg_save_area" };
  Type *tys[4] = { type_new(TY_INT), type_new(TY_INT),
                   ptr_to(type_new(TY_VOID)), ptr_to(type_new(TY_VOID)) };

  Member **link = &st->members;
  for (int i = 0; i < 4; i++) {
    Member *m = xmalloc(sizeof(Member));
    memset(m, 0, sizeof(Member));
    m->name = names[i];
    m->type = tys[i];
    *link = m;
    link = &m->next;
  }
  layout_struct(st);
  cached = st;
  return st;
}

/**
 * enum support: TK_ENUM is currently lexed but unparsed. Enumerators
 * become integer constants the parser knows at parse time (they have
 * to feed array dims, case labels and global initializers), and an
 * enum type is just an int, as the standard allows.
 */

/* enumerators live in their own parse-time namespace, like tags and
 * typedef names; a use resolves to the constant immediately */
typedef struct EnumConst EnumConst;
struct EnumConst {
  EnumConst *next;
  char *name;
  int val;
};

static EnumConst *enum_consts;

static EnumConst *find_enum_const(char *name) {
  for (EnumConst *e = enum_consts; e; e = e->next)
    if (strcmp(e->name, name) == 0)
      return e;
  return NULL;
}

static void register_enum_const(char *name, int val) {
  EnumConst *e = xmalloc(sizeof(EnumConst));
  e->name = name;
  e->val = val;
  e->next = enum_consts;
  enum_consts = e;
}

/* enum tags: names only, the type is always int */
typedef struct EnumTag EnumTag;
struct EnumTag {
  EnumTag *next;
  char *name;
};

static EnumTag *enum_tags;

static int has_enum_tag(char *name) {
  for (EnumTag *t = enum_tags; t; t = t->next)
    if (strcmp(t->name, name) == 0)
      return 1;
  return 0;
}

static void register_enum_tag(char *name) {
  EnumTag *t = xmalloc(sizeof(EnumTag));
  t->name = name;
  t->next = enum_tags;
  enum_tags = t;
}

/* a constant expression, folded for enumerator values; everything
 * else enumerated is not a constant */
/* constant-folds e; on success *ok stays 1 and the value comes
 * back, on failure *ok is 0. the caller decides whether the
 * expression "was not a constant" or is a VLA dimension */
static int try_eval_const(Node *e, int *ok) {
  switch (e->kind) {
  case ND_NUM:
    if (e->is_float)
      break;
    return e->val;
  case ND_UNARY:
    switch (e->op) {
    case '+': return try_eval_const(e->lhs, ok);
    case '-': return -try_eval_const(e->lhs, ok);
    case '~': return ~try_eval_const(e->lhs, ok);
    case '!': return !try_eval_const(e->lhs, ok);
    }
    break;
  case ND_BIN: {
    int lhs = try_eval_const(e->lhs, ok);
    if (!*ok)
      return 0;
    int rhs = try_eval_const(e->rhs, ok);
    if (!*ok)
      return 0;
    switch (e->op) {
    case '+': return lhs + rhs;
    case '-': return lhs - rhs;
    case '*': return lhs * rhs;
    case '/':
      if (!rhs)
        error("division by zero");
      return lhs / rhs;
    case '%':
      if (!rhs)
        error("division by zero");
      return lhs % rhs;
    case '&': return lhs & rhs;
    case '|': return lhs | rhs;
    case '^': return lhs ^ rhs;
    case OP_SHL: return lhs << rhs;
    case OP_SHR: return lhs >> rhs;
    case '<':  return lhs < rhs;
    case '>':  return lhs > rhs;
    case OP_LE: return lhs <= rhs;
    case OP_GE: return lhs >= rhs;
    case OP_EQ: return lhs == rhs;
    case OP_NE: return lhs != rhs;
    case OP_LOGAND: return lhs && rhs;
    case OP_LOGOR:  return lhs || rhs;
    }
    break;
  }
  case ND_SIZEOF:
    if (e->targ) {
      if (type_is_vla(e->targ))
        break;   /* sizeof(int[n]) is not a constant */
      return type_size(e->targ);
    }
    if (e->lhs->type) {
      if (type_is_vla(e->lhs->type))
        break;
      return type_size(e->lhs->type);
    }
    break;
  default:
    break;
  }
  *ok = 0;
  return 0;
}

static int eval_const(Node *e) {
  int ok = 1;
  int v = try_eval_const(e, &ok);
  if (!ok)
    error("enumerator value is not a constant");
  return v;
}

/* enum { A, B = 5, ... }; values start at 0 and step by 1 unless
 * "= <const expr>" is given */
static void parse_enumerators(void) {
  int value = 0;
  for (;;) {
    char *name = expect_ident("enumerator")->name;
    if (consume_punct("="))
      value = eval_const(parse_assign());
    register_enum_const(name, value++);
    if (!consume_punct(","))
      break;
    if (is_punct("}"))
      break;   /* trailing comma, C11 allows it */
  }
  expect_punct("}");
}

/* the attributes with real semantics: packed and aligned change
 * layout, noreturn is _Noreturn by another name; every other clause
 * gcc accepts is parsed and discarded */
typedef struct {
  int packed;
  int no_ret;
  int align;
} Attrs;

static int attr_name_is(char *name, char *a, char *b) {
  return strcmp(name, a) == 0 || (b && strcmp(name, b) == 0);
}

/* parse one __attribute__((...)) clause into a. the list may be
 * empty, the cases are attribute names each optionally parenthesized,
 * and the arguments are balanced and skipped, so the multi-argument
 * ones headers use (format(printf, 1, 2), nonnull(1, 2)) parse even
 * though nothing consumes them */
static void parse_attribute_clause(Attrs *a) {
  expect_punct("(");
  expect_punct("(");
  for (;;) {
    if (consume_punct(")"))
      break;   /* an empty list is legal */
    char *name = xstrndup(tok->loc, tok->len);
    tok = tok->next;
    int val = 0;
    if (consume_punct("(")) {
      if (attr_name_is(name, "aligned", "__aligned__")) {
        /* aligned(N): a constant power-of-two size, like _Alignas */
        if (!is_punct(")")) {
          Node *e = parse_assign();
          if (!is_const_expr(e))
            error_at(tok->loc, "aligned attribute value is not a constant");
          CVal cv = const_fold(e);
          if (cv.is_float || cv.val < 1 || (cv.val & (cv.val - 1)) != 0)
            error_at(tok->loc, "aligned attribute is not a positive power of 2");
          val = cv.val;
        }
      }
      int depth = 1;
      while (depth) {
        if (tok->kind == TK_EOF)
          error_at(tok->loc, "unterminated __attribute__");
        if (is_punct("("))
          depth++;
        else if (is_punct(")"))
          depth--;
        tok = tok->next;
      }
    }
    if (attr_name_is(name, "aligned", "__aligned__"))
      a->align = val;
    else if (attr_name_is(name, "packed", "__packed__"))
      a->packed = 1;
    else if (attr_name_is(name, "noreturn", "__noreturn__"))
      a->no_ret = 1;
    if (consume_punct(","))
      continue;
    expect_punct(")");
    break;
  }
  expect_punct(")");
}

/* every __attribute__ / __attribute clause in a row */
static Attrs parse_attrs(void) {
  Attrs a = {0};
  while (tok->kind == TK_ATTRIBUTE) {
    tok = tok->next;
    parse_attribute_clause(&a);
  }
  return a;
}

static Member *parse_struct_members(int is_union) {
  Member head = {0};
  Member **link = &head.next;
  int count = 0;
  int saw_fam = 0;

  while (!is_punct("}")) {
    if (tok->kind == TK_EOF)
      error_at(tok->loc, "unexpected EOF inside struct definition");
    if (saw_fam)
      error_at(tok->loc, "flexible array member must be the last member");
    int alignas = 0;
    Type *base = parse_typespec(&alignas);
    for (;;) {
      char *name;
      Type *mt = declarator(base, &name);
      Member *m = xmalloc(sizeof(Member));
      memset(m, 0, sizeof(Member));
      m->name = name;
      m->type = mt;
      m->align = alignas;
      if (consume_punct(":")) {
        /* a bit-field; the width is an integer constant expression.
         * zero width is the anonymous alignment marker */
        Node *w = parse_assign();
        if (!is_const_expr(w))
          error_at(tok->loc, "bit-field width must be a constant expression");
        CVal cv = const_fold(w);
        if (cv.is_float || cv.val < 0)
          error_at(tok->loc, "bit-field width must be a non-negative integer");
        if (mt->kind != TY_CHAR && mt->kind != TY_SHORT &&
            mt->kind != TY_INT && mt->kind != TY_LONG && !mt->is_bool)
          error_at(tok->loc, "invalid bit-field type");
        if (cv.val > mt->size * 8)
          error_at(tok->loc, "bit-field width too large for its type");
        if (cv.val == 0 && name)
          error_at(tok->loc, "named bit-field must have a non-zero width");
        m->is_bitfield = 1;
        m->bit_width = cv.val;
        if (alignas)
          error_at(tok->loc, "alignment specifier on a bit-field");
      } else if (!name) {
        error_at(tok->loc, "struct members must be named");
      }
      *link = m;
      link = &m->next;
      count++;
      /* C forbids variable-length array members entirely (6.7.5.2);
       * an incomplete array type ("int a[]") instead is a flexible
       * array member: it must be the last member, and it needs at
       * least one member before it (6.7.2.1) */
      if (type_is_vla(mt))
        error_at(tok->loc, "a struct cannot have a variable-length array member");
      if (mt->kind == TY_ARRAY && mt->array_len == 0)
        saw_fam = 1;
      if (!consume_punct(","))
        break;
    }
    expect_punct(";");
  }
  if (saw_fam) {
    if (is_union)
      error_at(tok->loc, "a union cannot have a flexible array member");
    if (count == 1)
      error_at(tok->loc, "flexible array member needs a member before it");
  }
  consume_punct("}");
  return head.next;
}

static int is_typespec_start(Token *t) {
  return t->kind == TK_VOID || t->kind == TK_CHAR || t->kind == TK_SHORT ||
         t->kind == TK_INT || t->kind == TK_LONG || t->kind == TK_SIGNED ||
         t->kind == TK_UNSIGNED || t->kind == TK_FLOAT ||
         t->kind == TK_DOUBLE || t->kind == TK_BOOL || t->kind == TK_STRUCT || t->kind == TK_ENUM ||
         t->kind == TK_UNION || t->kind == TK_CONST || t->kind == TK_VOLATILE ||
         t->kind == TK_STATIC || t->kind == TK_EXTERN || t->kind == TK_REGISTER ||
         t->kind == TK_ALIGNAS ||
         t->kind == TK_INLINE || t->kind == TK_RESTRICT || t->kind == TK_NORETURN ||
         t->kind == TK_TYPEOF || t->kind == TK_THREAD_LOCAL ||
         (t->kind == TK_IDENT && find_typedef(t->name) &&
          !typedef_ident_is_name(t));
}

/* any run of type keywords: "unsigned long long" etc. _Alignas is a
 * declaration-specifier that can sit anywhere in the run ("_Alignas(16)
 * int x" or "int _Alignas(16) x"); its value rides out through
 * alignas_ret (NULL means the context is a cast / sizeof / parameter,
 * where the specifier is illegal and is rejected) */
static Type *parse_typespec(int *alignas_ret) {
  int is_unsigned = 0;
  int is_const = 0;
  int is_volatile = 0;
  int longs = 0;
  int alignas = 0;
  Type *t = NULL;

  for (;;) {
    if (consume(TK_ALIGNAS)) {
      expect_punct("(");
      Node *a = parse_assign();
      expect_punct(")");
      if (!is_const_expr(a))
        error_at(tok->loc, "_Alignas value is not a constant");
      int v = const_fold(a).val;
      if (v < 1 || (v & (v - 1)) != 0)
        error_at(tok->loc, "requested alignment is not a positive power of 2");
      if (v > alignas)
        alignas = v;
      continue;
    }
    if (consume(TK_CONST))     { is_const = 1;    continue; }
    if (consume(TK_VOLATILE))  { is_volatile = 1; continue; }
    if (consume(TK_RESTRICT))  { continue; }   /* no-alias hint, ignored */
    if (consume(TK_UNSIGNED))  { is_unsigned = 1; continue; }
    if (consume(TK_SIGNED))    { continue; }
    if (consume(TK_LONG))      { longs++;         continue; }
    if (consume(TK_VOID))      { t = type_new(TY_VOID);   continue; }
    if (consume(TK_CHAR))      { t = type_new(TY_CHAR);   continue; }
    if (consume(TK_SHORT))     { t = type_new(TY_SHORT);  continue; }
    if (consume(TK_INT)) {
      /* "int" after long/short just spells the integer kind; it must
       * not override the long/short already chosen ("long int" is
       * long, "short int" is short, "long long int" is long long) */
      if (!t)
        t = type_new(TY_INT);
      continue;
    }
    if (consume(TK_FLOAT))     { t = type_new(TY_FLOAT);  continue; }
    if (consume(TK_DOUBLE))    { t = type_new(TY_DOUBLE); continue; }
    if (consume(TK_BOOL)) {
      /* _Bool is a 1-byte object whose stored value is always 0 or 1;
       * the codegen normalizes every write to it. it rides on TY_CHAR
       * so storage size and layout come for free */
      t = type_new(TY_CHAR);
      t->is_bool = 1;
      continue;
    }
    if (consume(TK_ENUM)) {
      char *tag = NULL;
      if (at(TK_IDENT))
        tag = expect(TK_IDENT, "enum tag")->name;
      if (consume_punct("{")) {
        /* the tag goes in before the body, so the body can
         * reference it (sizeof(enum x)) */
        if (tag)
          register_enum_tag(tag);
        parse_enumerators();
      } else if (!tag) {
        error_at(tok->loc, "expected enum tag");
      } else if (!has_enum_tag(tag)) {
        error_at(tok->loc, "unknown enum '%s'", tag);
      }
      t = type_new(TY_INT);
      t->is_const = is_const;
      t->is_volatile = is_volatile;
      if (alignas_ret)
        *alignas_ret = alignas;
      else if (alignas)
        error_at(tok->loc, "alignment specifier not allowed here");
      return t;
    }
    if (consume(TK_UNION)) {
      /* "union __attribute__((packed)) U": the attribute addresses
       * the tag itself, so it has to land on the type before the
       * layout that the closing brace runs */
      Attrs a = parse_attrs();
      char *tag = NULL;
      if (at(TK_IDENT))
        tag = expect(TK_IDENT, "union tag")->name;
      if (consume_punct("{")) {
        /* the tag goes in before the members, so the body can
         * reference itself (union Node *next); completing a tag
         * that was only forward-declared fills in the placeholder
         * so earlier typedefs of it see the members too */
        Type *ut = tag ? find_tag(tag) : NULL;
        if (!ut) {
          ut = union_type();
          if (tag)
            register_tag(tag, ut);
        }
        if (a.packed)
          ut->is_packed = 1;
        if (a.align > ut->align)
          ut->align = a.align;
        ut->members = parse_struct_members(1);
        if (!ut->members)
          error_at(tok->loc, "empty union");
        layout_union(ut);
        t = ut;
        continue;
      }
      if (!tag)
        error_at(tok->loc, "expected union tag");
      t = find_tag(tag);
      if (!t || t->kind != TY_UNION) {
        /* an unknown tag in "union X" is a forward declaration;
         * it stays incomplete until a definition appears */
        t = union_type();
        if (a.packed)
          t->is_packed = 1;
        register_tag(tag, t);
      }
      continue;
    }
    if (consume(TK_STRUCT)) {
      /* "struct __attribute__((packed)) S": the attribute addresses
       * the tag itself, so it has to land on the type before the
       * layout that the closing brace runs */
      Attrs a = parse_attrs();
      char *tag = NULL;
      if (at(TK_IDENT))
        tag = expect(TK_IDENT, "struct tag")->name;
      if (consume_punct("{")) {
        /* the tag goes in before the members, so the body can
         * reference itself (struct Node *next); completing a tag
         * that was only forward-declared fills in the placeholder
         * so earlier typedefs of it see the members too */
        Type *st = tag ? find_tag(tag) : NULL;
        if (!st) {
          st = struct_type();
          if (tag)
            register_tag(tag, st);
        }
        if (a.packed)
          st->is_packed = 1;
        if (a.align > st->align)
          st->align = a.align;
        st->members = parse_struct_members(0);
        if (!st->members)
          error_at(tok->loc, "empty struct");
        layout_struct(st);
        t = st;
        continue;
      }
      if (!tag)
        error_at(tok->loc, "expected struct tag");
      t = find_tag(tag);
      if (!t) {
        /* an unknown tag in "struct X" (no braces) is a forward
         * declaration; the type is incomplete until a definition
         * appears, which is enough for pointers to it */
        t = struct_type();
        register_tag(tag, t);
      }
      continue;
    }
if (consume(TK_TYPEOF)) {
      /* typeof(int) is a type; typeof(x) is the type of existing
       * variable x, "known" only because the parser recorded it. */
      expect_punct("(");
      Type *tt;
      if (is_typespec_start(tok)) {
        char *dummy;
        tt = declarator(parse_typespec(NULL), &dummy);
      } else {
        tt = infer_type(parse_assign());
      }
      expect_punct(")");
      if (is_const || is_volatile || is_unsigned || longs) {
        /* trailing qualifiers ("const typeof(x) y") must land on a
         * copy, never on the type the registered variable shares */
        Type *copy = xmalloc(sizeof(Type));
        *copy = *tt;
        tt = copy;
      }
      t = tt;
      continue;
    }
    if (at(TK_IDENT)) {
      Type *tt = find_typedef(tok->name);
      /* same lookahead as is_typespec_start: a typedef-name before a
       * declaration-ending punct is the name being declared, not the
       * type ("typedef char T;", "int T = 5;") */
      if (tt && !typedef_ident_is_name(tok)) {
        tok = tok->next;
        if (longs || is_unsigned || is_const || is_volatile) {
          /* modifiers apply to a copy so the alias itself is never
           * mutated; the unmodified case shares the registry entry,
           * which keeps a forward-declared struct completed later
           * visible through the alias */
          t = xmalloc(sizeof(Type));
          *t = *tt;
          if (longs && (t->kind == TY_INT || t->kind == TY_LONG))
            t->kind = TY_LONG;
          if (longs >= 2)
            t->is_longlong = 1;
          if (is_unsigned)
            t->is_unsigned = 1;
          if (is_const)
            t->is_const = 1;
          if (is_volatile)
            t->is_volatile = 1;
        } else {
          t = tt;
        }
        continue;   /* let trailing qualifiers ("myint const") apply */
      }
    }
    break;
  }

  if (!t)
    t = type_new(longs ? TY_LONG : TY_INT);
  else if (longs && t->kind == TY_INT)
    /* "long int" / "long long int": the int already materialized a
     * TY_INT, fold it to long now that the run is over */
    t = type_new(TY_LONG);
  if (longs >= 2)
    t->is_longlong = 1;
  t->is_unsigned = is_unsigned;
  t->is_const = is_const;
  t->is_volatile = is_volatile;
  if (alignas_ret)
    *alignas_ret = alignas;
  else if (alignas)
    error_at(tok->loc, "alignment specifier not allowed here");
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
  memset(n, 0, sizeof(Node));
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
static Node *parse_stmt(void);
static Node *parse_initializer(void);

static Node *parse_expr(void) {
  /* the comma operator: lowest precedence, left assoc, sequence
   * point, the value is the right operand */
  Node *node = parse_assign();
  while (consume_punct(","))
    node = new_binary(',', node, parse_assign());
  return node;
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

static Node *postfix_suffix(Node *node);

static Node *parse_unary(void) {
  /* GNU label-as-value: "&&name" is the address of the label in this
   * function, usable as a void*; the && token cannot be a binary
   * operator in unary position, so an identifier after it can only
   * be a label */
  if (tok->kind == TK_PUNCT && to_op() == OP_LOGAND && tok->next &&
      tok->next->kind == TK_IDENT) {
    Token *lt = tok->next;
    tok = lt->next;
    Node *n = node_new(ND_LABEL_ADDR);
    n->name = lt->name;
    n->label = -1;   /* the number is assigned by collect_labels */
    return n;
  }
  if (consume_punct("+")) return new_unary('+', parse_unary());
  if (consume_punct("-")) return new_unary('-', parse_unary());
  if (consume_punct("!")) return new_unary('!', parse_unary());
  if (consume_punct("~")) return new_unary('~', parse_unary());
  if (consume_punct("*")) return new_unary('*', parse_unary());
  if (consume_punct("&")) return new_unary('&', parse_unary());

  /* (typename) cast, or a C99 compound literal when a brace follows
   * the type name: `(struct point){1,2}`. the literal is an anonymous
   * initialized object, so it goes around parse_primary and straight
   * into the same postfix loop a primary would get */
  if (is_punct("(") && tok->next && is_typespec_start(tok->next)) {
    tok = tok->next;
    char *dummy;
    Type *ty = declarator(parse_typespec(NULL), &dummy);
    expect_punct(")");
    if (is_punct("{")) {
      Node *n = node_new(ND_COMP_LIT);
      n->targ = ty;
      n->elems = parse_initializer();
      return postfix_suffix(n);
    }
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

/* the postfix suffix loop: calls, indexing, member access, ++/--.
 * a compound literal skips parse_primary but still goes through
 * postfix_suffix, so `(struct point){1,2}.x` works */
static Node *postfix_suffix(Node *node) {
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

static Node *parse_postfix(void) {
  return postfix_suffix(parse_primary());
}

/* -------- stdarg builtins -------- */

/* sysV bookkeeping: how many of each register class the named
 * (fixed) parameters consume, and how many spill to the stack. the
 * "~ret" hidden param of a struct-returning variadic function counts
 * as one more integer-class arg, mirroring the resolve pass */
static void va_param_counts(Type *ft, int *gp, int *sse, int *stk) {
  *gp = 0;
  *sse = 0;
  *stk = 0;
  for (Node *p = ft->params; p; p = p->next) {
    if (!p->name)
      continue;
    if (p->type->kind == TY_FLOAT || p->type->kind == TY_DOUBLE) {
      (*sse)++;
      if (*sse > 8)
        (*stk)++;
    } else {
      (*gp)++;
      if (*gp > 6)
        (*stk)++;
    }
  }
  if (ft->ret->kind == TY_STRUCT || ft->ret->kind == TY_UNION) {
    (*gp)++;
    if (*gp > 6)
      (*stk)++;
  }
}

static Node *parse_va_start(void) {
  if (!cur_fn || !cur_fn->is_variadic)
    error_at(tok->loc, "va_start outside a variadic function");
  expect_punct("(");
  Node *ap = parse_assign();
  expect_punct(",");
  Token *last = expect(TK_IDENT, "last named parameter after va_start");
  expect_punct(")");

  int gp, sse, stk;
  va_param_counts(cur_fn, &gp, &sse, &stk);
  int found = 0;
  for (Node *p = cur_fn->params; p; p = p->next)
    if (p->name && strcmp(p->name, last->name) == 0) {
      found = 1;
      break;
    }
  if (!found)
    error_at(last->loc, "unknown parameter '%s' in va_start", last->name);

  Node *n = node_new(ND_VA_START);
  n->lhs = ap;
  n->va[0] = 8 * gp;
  n->va[1] = (gp == 6) ? 48 + 16 * sse : 48;
  n->va[2] = 16 + 8 * stk;   /* first stack vararg, rbp-relative */
  n->va[3] = 0;              /* the register save area, set at resolve */
  return n;
}

static Node *parse_va_arg(void) {
  expect_punct("(");
  Node *ap = parse_assign();
  expect_punct(",");
  char *dummy;
  Type *ty = declarator(parse_typespec(NULL), &dummy);
  expect_punct(")");

  if (ty->kind == TY_VOID || ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    error_at(tok->loc, "unsupported va_arg type");
  Node *n = node_new(ND_VA_ARG);
  n->lhs = ap;
  n->targ = ty;
  return n;
}

static Node *parse_va_end(void) {
  expect_punct("(");
  parse_assign();
  expect_punct(")");
  Node *n = node_new(ND_NUM);
  n->val = 0;
  return n;
}

/* va_copy(dst, src): a whole-struct copy of the two va_lists */
static Node *parse_va_copy(void) {
  expect_punct("(");
  Node *dst = parse_assign();
  expect_punct(",");
  Node *src = parse_assign();
  expect_punct(")");

  Node *ad = node_new(ND_UNARY);
  ad->op = '&';
  ad->lhs = dst;
  Node *dd = node_new(ND_UNARY);
  dd->op = '*';
  dd->lhs = ad;
  Node *as = node_new(ND_UNARY);
  as->op = '&';
  as->lhs = src;
  Node *ds = node_new(ND_UNARY);
  ds->op = '*';
  ds->lhs = as;
  Node *n = node_new(ND_ASSIGN);
  n->op = '=';
  n->lhs = dd;
  n->rhs = ds;
  return n;
}

/* _Generic(expr, type-name: arm, ..., default: arm). the controlling
 * expression is parsed but not evaluated; the association list turns
 * into a chain of nodes each carrying a type-name (targ) and its arm
 * (lhs). selection happens at resolve time, once the controlling
 * expression has a type */
static Node *parse_generic(void) {
  expect_punct("(");
  Node *n = node_new(ND_GENERIC);
  n->cond = parse_assign();
  expect_punct(",");

  Node *head = NULL, **tail = &head;
  for (;;) {
    Node *a = node_new(ND_GENERIC);
    if (consume(TK_DEFAULT)) {
      expect_punct(":");
      a->lhs = parse_assign();
    } else {
      char *dummy;
      a->targ = declarator(parse_typespec(NULL), &dummy);
      expect_punct(":");
      a->lhs = parse_assign();
    }
    *tail = a;
    tail = &a->next;
    if (consume_punct(","))
      continue;
    break;
  }
  expect_punct(")");
  n->els = head;
  return n;
}

static Node *parse_primary(void) {
  if (consume(TK_EXTENSION)) {
    /* __extension__ exists only to quiet -pedantic; it shields a
     * statement expression or typeof that follows, so it parses to
     * nothing before the real operand */
    return parse_unary();
  }
  if (at(TK_ALIGNOF)) {
    tok = tok->next;
    Type *ty = NULL;
    if (is_punct("(") && tok->next && is_typespec_start(tok->next)) {
      tok = tok->next;
      char *dummy;
      ty = declarator(parse_typespec(NULL), &dummy);
      expect_punct(")");
    }
    Node *n = node_new(ND_ALIGNOF);
    if (ty)
      n->targ = ty;
    else
      n->lhs = parse_unary();
    return n;
  }

  if (at(TK_GENERIC)) {
    tok = tok->next;
    return parse_generic();
  }

  if (at(TK_SIZEOF)) {
    tok = tok->next;
    Type *ty = NULL;
    if (is_punct("(") && tok->next && is_typespec_start(tok->next)) {
      tok = tok->next;
      char *dummy;
      ty = declarator(parse_typespec(NULL), &dummy);
      expect_punct(")");
    }

    Node *n = node_new(ND_SIZEOF);
    if (ty) {
      n->targ = ty;
      /* a variable-length array has no compile-time size; the
       * dimension expressions come along for the ride and a runtime
       * size is computed where it is used */
      if (type_is_vla(ty))
        n->vla_sz = vla_size_expr(ty);
    } else {
      n->lhs = parse_unary();
    }
    return n;
  }

  if (is_punct("(") && is_punct_next("{")) {
    /* GNU statement expression: ({ stmt; ...; value; }) is an
     * expression whose value is the last expression statement. the
     * block is parsed exactly like a function body's, declarations
     * and all; the "})" terminator is what tells this "(" from an
     * ordinary parenthesized expression or a compound literal,
     * whose brace never follows "(" directly */
    tok = tok->next->next;
    Node *n = node_new(ND_STMT_EXPR);
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
    expect_punct(")");
    n->body = head.next;
    if (n->body) {
      Node *last = n->body;
      while (last->next)
        last = last->next;
      if (last->kind == ND_EXPR_STMT && last->lhs)
        n->then = last->lhs;
    }
    return n;
  }

  if (consume_punct("(")) {
    Node *node = parse_expr();
    expect_punct(")");
    return node;
  }

  Token *t;
  if (tok->kind >= TK_VOID && tok->kind < TK_PUNCT) {
    /* a keyword token is its own enumerator: TK_VOID, TK_IF, ...
     * so "int x = TK_VOID;" works, which is how the compiler's own
     * keyword table is written: {"void", TK_VOID} */
    Node *n = node_new(ND_NUM);
    n->val = tok->kind;
    tok = tok->next;
    return n;
  }

  if ((t = consume(TK_NUM))) {
    Node *n = node_new(ND_NUM);
    if (t->is_float) {
      n->is_float = 1;
      n->is_f = t->is_f;
      n->fval = t->fval;
    } else {
      n->val = t->val;
      n->is_unsigned = t->is_unsigned;
      n->is_long = t->is_long;
    }
    return n;
  }

  if ((t = consume(TK_STR))) {
    Node *n = node_new(ND_STR);
    n->str = t->str;
    n->str_len = t->str_len;
    /* adjacent string literals ("a" "b") coalesce into one, per C */
    while (at(TK_STR)) {
      Token *u = tok;
      tok = tok->next;
      char *buf = xmalloc(n->str_len + u->str_len + 1);
      memcpy(buf, n->str, n->str_len);
      memcpy(buf + n->str_len, u->str, u->str_len);
      buf[n->str_len + u->str_len] = '\0';
      n->str = buf;
      n->str_len = n->str_len + u->str_len;
    }
    return n;
  }

  if ((t = consume(TK_IDENT))) {
    /* stdarg builtins: va_start / va_arg / va_end / va_copy */
    if (strcmp(t->name, "va_start") == 0)
      return parse_va_start();
    if (strcmp(t->name, "va_arg") == 0)
      return parse_va_arg();
    if (strcmp(t->name, "va_end") == 0)
      return parse_va_end();
    if (strcmp(t->name, "va_copy") == 0)
      return parse_va_copy();
    /* GNU builtins: every one folds at parse time, so none of them
     * needs a node kind of its own */
    if (strcmp(t->name, "__builtin_expect") == 0) {
      /* __builtin_expect(e, v): the value of e; the hint half is
       * parsed and dropped, since there is no branch-prediction pass
       * to honor it */
      expect_punct("(");
      Node *exp = parse_assign();
      expect_punct(",");
      parse_assign();
      expect_punct(")");
      return exp;
    }
    if (strcmp(t->name, "__builtin_unreachable") == 0) {
      /* a no-op marker; folds to a constant so it is legal anywhere
       * an expression is */
      expect_punct("(");
      expect_punct(")");
      Node *n = node_new(ND_NUM);
      n->val = 0;
      return n;
    }
    if (strcmp(t->name, "__builtin_constant_p") == 0) {
      /* 1 when the argument is an integer constant expression, 0
       * otherwise; the argument itself is never evaluated, only its
       * constness is wanted. a string literal counts, as its address
       * is a link-time constant (gcc agrees) */
      expect_punct("(");
      Node *e = parse_assign();
      expect_punct(")");
      Node *n = node_new(ND_NUM);
      n->val = (e->kind == ND_STR || is_const_expr(e)) ? 1 : 0;
      return n;
    }
    if (strcmp(t->name, "__builtin_types_compatible_p") == 0) {
      /* 1 when the two type names are compatible, top-level
       * qualifiers ignored on both sides, arrays and functions left
       * un-decayed (an incomplete array matches any bound) */
      expect_punct("(");
      char *dummy;
      Type *t1 = declarator(parse_typespec(NULL), &dummy);
      expect_punct(",");
      Type *t2 = declarator(parse_typespec(NULL), &dummy);
      expect_punct(")");
      Node *n = node_new(ND_NUM);
      n->val = types_compatible(t1, t2);
      return n;
    }
    if (strcmp(t->name, "__builtin_choose_expr") == 0) {
      /* a when the condition is a nonzero integer constant, b when
       * zero; the other arm is parsed but only the chosen one is
       * resolved and code-generated, the _Generic one-shot rule, so
       * a loser's errors or side effects never surface */
      expect_punct("(");
      Node *c = parse_assign();
      expect_punct(",");
      if (!is_const_expr(c))
        error_at(tok->loc, "__builtin_choose_expr condition is not a constant");
      Node *a = parse_assign();
      expect_punct(",");
      Node *b = parse_assign();
      expect_punct(")");
      CVal cv = const_fold(c);
      if (cv.is_float)
        error_at(tok->loc, "__builtin_choose_expr condition is not an integer constant");
      return cv.val ? a : b;
    }
    EnumConst *ec = find_enum_const(t->name);
    if (ec) {
      /* an enumerator is a compile-time int; it resolves here, so it
       * works anywhere a constant is expected */
      Node *n = node_new(ND_NUM);
      n->val = ec->val;
      return n;
    }
    Node *n = node_new(ND_VAR);
    n->name = t->name;
    return n;
  }

  error_at(tok->loc, "expected expression");
}

/* -------- statements -------- */

static Node *parse_declaration(void);
static Node *parse_stmt(void);
static Node *parse_block(void);
static void parse_typedef(void);
static void parse_static_assert(void);

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

  if (consume(TK_SWITCH)) {
    expect_punct("(");
    Node *cond = parse_expr();
    expect_punct(")");

    Node *n = node_new(ND_SWITCH);
    n->cond = cond;
    nswitch++;
    expect_punct("{");
    n->body = parse_block();
    nswitch--;
    return n;
  }

  if (consume(TK_CASE)) {
    if (!nswitch)
      error_at(tok->loc, "case label outside a switch");
    Node *n = node_new(ND_CASE);
    n->lhs = parse_assign();
    if (is_punct("...")) {   /* GNU case range: case lo ... hi: */
      tok = tok->next;
      n->rhs = parse_assign();
    }
    expect_punct(":");
    n->body = parse_stmt();
    return n;
  }

  if (consume(TK_DEFAULT)) {
    if (!nswitch)
      error_at(tok->loc, "case label outside a switch");
    expect_punct(":");
    Node *n = node_new(ND_CASE);
    n->body = parse_stmt();
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
      if (is_typespec_start(tok) || tok->kind == TK_ATTRIBUTE ||
          tok->kind == TK_EXTENSION)
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
    if (!nloop && !nswitch)
      error_at(tok->loc, "break outside of loop or switch");
    expect_punct(";");
    return node_new(ND_BREAK);
  }

  if (consume(TK_CONTINUE)) {
    if (!nloop)
      error_at(tok->loc, "continue outside of loop");
    expect_punct(";");
    return node_new(ND_CONTINUE);
  }

  if (consume(TK_GOTO)) {
    if (consume_punct("*")) {
      /* goto *p: an indirect jump through a label address */
      Node *n = node_new(ND_GOTO_PTR);
      n->lhs = parse_expr();
      expect_punct(";");
      return n;
    }
    Node *n = node_new(ND_GOTO);
    n->name = expect(TK_IDENT, "label name")->name;
    expect_punct(";");
    return n;
  }

  /* "name : stmt" is a label. an identifier followed by ":" can never
   * start a declaration here (declarations are handled above via
   * is_typespec_start), so no ambiguity with typedef names or casts */
  if (tok->kind == TK_IDENT && tok->next &&
      tok->next->kind == TK_PUNCT && tok->next->len == 1 &&
      tok->next->loc[0] == ':') {
    Token *ident = expect_ident("label name");
    Node *n = node_new(ND_LABEL);
    n->name = ident->name;
    expect_punct(":");
    n->body = parse_stmt();
    return n;
  }

  if (consume(TK_TYPEDEF)) {
    parse_typedef();
    return NULL;
  }

  if (consume(TK_STATIC_ASSERT)) {
    parse_static_assert();
    return NULL;
  }

  if (is_typespec_start(tok) || tok->kind == TK_ATTRIBUTE ||
        tok->kind == TK_EXTENSION) {
    Node *s = parse_declaration();
    for (Node *m = s; m; m = m->next)
      if (m->kind == ND_FUNC && m->body)
        /* the body's parse already swallowed the enclosing block */
        error("function '%s' defined inside a block, unsupported", m->name);
    return s;
  }

  /* expression statement; a bare ';' also lands here */
  Node *e = node_new(ND_EXPR_STMT);
  if (consume_punct(";"))
    return e;
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
        if (tok->kind == TK_PUNCT && tok->len == 3 &&
            memcmp(tok->loc, "...", 3) == 0) {
          /* "..." marks the function variadic; the extra args ride
           * the same registers as fixed params on x86-64 SysV */
          ft->is_variadic = 1;
          tok = tok->next;
          expect_punct(")");
          break;
        }
        consume(TK_REGISTER);   /* hint, ignored like the locals */
        Type *pt = parse_typespec(NULL);
        char *pname = NULL;
        pt = declarator(pt, &pname);   /* abstract declarators allowed */
        parse_attrs();   /* "int x __attribute__((unused))": on a
                            parameter the clause has no storage to
                            change, so it parses and fades */

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
  Type *dims[64];
  int dim_n = 0;
  for (;;) {
    if (consume_punct("[")) {
      /* each dimension is kept as a Type that carries either a
       * constant length (array_len) or a size expression (vla_len);
       * the throwaway "int [len]" carrier is never used as-is */
      Type *dimty = NULL;
      if (at(TK_NUM)) {
        dimty = array_of(type_new(TY_INT), tok->val);
        tok = tok->next;
      } else if (at(TK_IDENT)) {
        EnumConst *ec = find_enum_const(tok->name);
        if (ec) {
          dimty = array_of(type_new(TY_INT), ec->val);
          tok = tok->next;
        }
      }
      if (!dimty && !is_punct("]")) {
        /* an arbitrary expression: "int a[ARRAY_LEN(b) == 3 ? 1 :
         * -1];" folds to an integer and is discarded; one that does
         * not fold is a variable-length dimension, kept as an
         * expression to evaluate at run time */
        Node *e = parse_assign();
        int ok = 1;
        int len = try_eval_const(e, &ok);
        if (ok)
          dimty = array_of(type_new(TY_INT), len);
        else
          dimty = vla_array_of(type_new(TY_INT), e);
      }
      expect_punct("]");
      if (!dimty)
        dimty = array_of(type_new(TY_INT), 0);   /* "int a[]" */
      if (dim_n < 64)
        dims[dim_n++] = dimty;
      continue;
    }
    if (is_punct("(")) {
      /* a function suffix binds to whatever the dims have built so
       * far, so flush them first */
      for (int i = dim_n - 1; i >= 0; i--)
        t = dims[i]->vla_len ? vla_array_of(t, dims[i]->vla_len)
                             : array_of(t, dims[i]->array_len);
      dim_n = 0;
      t = parse_params(t);
      continue;
    }
    /* brackets read left to right are outermost first, so the type
     * nests them in reverse: x[2][4] is array[2] of array[4] of base */
    for (int i = dim_n - 1; i >= 0; i--)
      t = dims[i]->vla_len ? vla_array_of(t, dims[i]->vla_len)
                           : array_of(t, dims[i]->array_len);
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
  while (consume_punct("*")) {
    t = ptr_to(t);
    if (consume(TK_CONST))
      t->is_const = 1;   /* "char * const p": the pointer is const */
    if (consume(TK_VOLATILE))
      t->is_volatile = 1;   /* "char * volatile p": the pointer is volatile */
    consume(TK_RESTRICT);   /* "char * restrict p": a no-alias hint the
                               backend cannot use, dropped at codegen */
  }

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
/* a brace initializer: { e1, e2, ... }, nested lists for aggregates.
 * empty lists ({}) zero-fill the object, and a trailing comma is
 * legal, both per C's grammar */
/* one element of a brace initializer: a plain initializer, or a
 * designator list applied to one. a designator is .ident or
 * [const-expr]; the list ends at '=', e.g. .a.b[2] = v. the first
 * designator is the outermost, so later ones nest under it */
static Node *parse_init_elem(void) {
  Node *root = NULL;   /* first designator, outermost */
  Node **tail = &root;
  for (;;) {
    if (consume_punct(".")) {
      Node *d = node_new(ND_DESIG);
      d->name = expect_ident("designator")->name;
      d->lhs = NULL;
      *tail = d;
      tail = &d->then;
      continue;
    }
    if (is_punct("[")) {
      tok = tok->next;
      Node *d = node_new(ND_DESIG);
      d->name = NULL;
      d->lhs = parse_assign();
      expect_punct("]");
      *tail = d;
      tail = &d->then;
      continue;
    }
    break;
  }
  if (root) {
    expect_punct("=");
    *tail = parse_initializer();
    return root;
  }
  return parse_initializer();
}

static Node *parse_initializer(void) {
  if (!is_punct("{"))
    return parse_assign();

  tok = tok->next;
  Node *n = node_new(ND_INIT_LIST);
  Node head = {0};
  Node **link = &head.next;

  for (;;) {
    if (consume_punct("}"))
      break;
    Node *e = parse_init_elem();
    *link = e;
    link = &e->next;
    if (consume_punct("}"))
      break;
    expect_punct(",");
  }
  n->elems = head.next;
  return n;
}

static Node *parse_declarator(Type *base, Type **out) {
  char *name;
  Type *t = declarator(base, &name);
  *out = t;

  if (t->kind == TY_FUNC) {
    Node *n = node_new(ND_FUNC);
    n->name = name;
    n->type = t;
    /* the body is parsed by parse_declaration, once any post-
     * declarator __attribute__ has been consumed ("int f(void)
     * __attribute__((noreturn)) { ... }") */
    return n;
  }

  Node *n = node_new(ND_DECL);
  n->name = name;
  n->type = t;

  /* the declared name is already in scope for its own initializer
   * ("char ch = (typeof(ch))97;"), so it registers before the '=' */
  if (name)
    register_parse_var(name, t);

  if (consume_punct("="))
    n->init = parse_initializer();
  return n;
}

/* __func__ / __FUNCTION__: C99 6.4.2.2 declares __func__ as a
 * predefined identifier in every function body, an implicit
 * static const char __func__[] = "name"; __FUNCTION__ is the GNU
 * synonym with the same value. both are real static arrays (one per
 * function, one per pipeline call), prepended to the body's statement
 * chain, so they decay like any other named array and sizeof works */
static Type *func_name_type(char *name) {
  Type *ty = array_of(type_new(TY_CHAR), strlen(name) + 1);
  ty->is_const = 1;
  return ty;
}

static Node *func_name_decl(char *varname, char *name) {
  Node *s = node_new(ND_STR);
  s->str = name;
  s->str_len = strlen(name);
  Node *d = node_new(ND_DECL);
  d->name = varname;
  d->type = func_name_type(name);
  d->is_static = 1;
  d->init = s;
  return d;
}

static Node *parse_function_body(Type *t, char *name) {
  expect_punct("{");
  /* parameters and the function's own name join the parse scope
   * so typeof(a), typeof(f) and typeof(f()) inside the body
   * resolve; the implicit __func__/__FUNCTION__ identifiers register
   * before the body parses too, so typeof(__func__) works anywhere */
  for (Node *p = t->params; p; p = p->next)
    if (p->name)
      register_parse_var(p->name, p->type);
  if (name) {
    register_parse_var(name, t);
    register_parse_var("__func__", func_name_type(name));
    register_parse_var("__FUNCTION__", func_name_type(name));
  }
  cur_fn = t;
  Node *blk = parse_block();
  cur_fn = NULL;
  if (name) {
    Node *f = func_name_decl("__func__", name);
    f->next = func_name_decl("__FUNCTION__", name);
    f->next->next = blk->body;
    blk->body = f;
  }
  return blk;
}

/* "typedef <typespec> <declarator>, ...;": the declarator's type is
 * registered under its name, no storage is created */

static void parse_typedef(void) {
  /* a storage class after "typedef" ("typedef static int T;") is a
   * constraint violation; reject the ones parse_declaration knows */
  if (tok->kind == TK_STATIC || tok->kind == TK_EXTERN ||
      tok->kind == TK_REGISTER || tok->kind == TK_INLINE ||
      tok->kind == TK_NORETURN || tok->kind == TK_THREAD_LOCAL)
    error_at(tok->loc, "storage class on a typedef");
  for (;;) {
    Token *start = tok;
    char *name;
    int alignas = 0;
    Type *t = parse_typespec(&alignas);
    if (alignas)
      error_at(tok->loc, "alignment specifier on a typedef");
    /* the typespec consumed nothing, so a known typedef-name at the
     * front means no type was given ("typedef T;") */
    if (tok == start && start->kind == TK_IDENT && find_typedef(start->name))
      error_at(tok->loc, "typedef name required");
    t = declarator(t, &name);
    Attrs a = parse_attrs();
    if (a.packed) {
      if (t->kind != TY_STRUCT && t->kind != TY_UNION)
        error_at(tok->loc, "packed attribute applies only to a struct or union");
      t->is_packed = 1;
      if (t->kind == TY_STRUCT)
        layout_struct(t);
      else
        layout_union(t);
    }
    if (a.align) {
      /* aligned on a typedef lands on the type, on a copy so the
       * alias it produced in parse_typespec is not mutated */
      Type *copy = xmalloc(sizeof(Type));
      *copy = *t;
      copy->align = a.align;
      t = copy;
    }
    if (!name)
      error_at(tok->loc, "typedef name required");
    register_typedef(name, t);
    if (!consume_punct(","))
      break;
  }
  expect_punct(";");
}

/* _Static_assert(cond, "msg");  the condition is an integer constant
 * expression, and a zero value is a compile-time error that reports
 * the given message, exactly as gcc does */
static void parse_static_assert(void) {
  expect_punct("(");
  Node *cond = parse_assign();
  int ok = 1;
  int v = try_eval_const(cond, &ok);
  if (!ok)
    error_at(tok->loc, "static assertion condition is not a constant");
  expect_punct(",");
  Token *msg = expect(TK_STR, "string literal in _Static_assert");
  expect_punct(")");
  expect_punct(";");
  if (!v)
    error_at(tok->loc, "static assertion failed: %s", msg->str);
}

static Node *parse_declaration(void) {
  /* storage class and function specifiers: the flags live on the
   * produced nodes; typedef with a storage class is rejected below.
   * register is a hint the backend ignores (every local already
   * lives in the frame and spills to memory only on call), so it
   * sets no flag. inline and _Noreturn are function-specifiers, so
   * applying them to an object is rejected after the declarator.
   * __extension__ is a no-op and __attribute__(()) folds its
   * semantics into attrs, which the declarator loop applies */
  int is_static = 0, is_extern = 0, is_reg = 0;
  int is_inline = 0, is_noreturn = 0, is_thread = 0;
  Attrs attrs = {0};
  for (;;) {
    if (consume(TK_STATIC))
      is_static = 1;
    else if (consume(TK_EXTERN))
      is_extern = 1;
    else if (consume(TK_REGISTER))
      is_reg = 1;
    else if (consume(TK_INLINE))
      is_inline = 1;
    else if (consume(TK_NORETURN))
      is_noreturn = 1;
    else if (consume(TK_THREAD_LOCAL))
      is_thread = 1;
    else if (consume(TK_EXTENSION))
      continue;
    else if (tok->kind == TK_ATTRIBUTE) {
      Attrs a = parse_attrs();
      attrs.packed |= a.packed;
      attrs.no_ret |= a.no_ret;
      if (a.align > attrs.align)
        attrs.align = a.align;
    } else
      break;
  }

  if (consume(TK_STATIC_ASSERT)) {
    if (is_static || is_extern || is_reg || is_inline || is_noreturn ||
        is_thread)
      error_at(tok->loc, "storage class on a _Static_assert");
    parse_static_assert();
    return NULL;
  }

  if (consume(TK_TYPEDEF)) {
    if (is_static || is_extern || is_reg || is_inline || is_noreturn ||
        is_thread)
      error_at(tok->loc, "storage class on a typedef");
    parse_typedef();
    return NULL;
  }
  Node *first = NULL;
  Node **link = &first;
  int alignas = 0;
  /* _Alignas can also be a function-return specifier ("_Alignas(16)
   * int f(void)"), which C forbids; the collector below catches the
   * object case and the function case is rejected after parsing */
  Type *base = parse_typespec(&alignas);

  /* the attribute between the type and the declarator ("struct S
   * {...} __attribute__((packed)) x;", "int __attribute__((unused))
   * x;") rides the type that parse_typespec just left */
  Attrs basea = parse_attrs();
  attrs.packed |= basea.packed;
  attrs.no_ret |= basea.no_ret;
  if (basea.align > attrs.align)
    attrs.align = basea.align;
  if (attrs.packed) {
    if (base->kind != TY_STRUCT && base->kind != TY_UNION)
      error_at(tok->loc, "packed attribute applies only to a struct or union");
    base->is_packed = 1;
    if (base->kind == TY_STRUCT)
      layout_struct(base);
    else
      layout_union(base);
  }
  if (attrs.align > alignas)
    alignas = attrs.align;

  for (;;) {
    Type *t;
    Node *n = parse_declarator(base, &t);
    Attrs a = parse_attrs();   /* post-declarator "int x __attribute__(...)" */
    if (a.packed)
      error_at(tok->loc, "packed attribute applies only to a struct or union type");
    if (a.no_ret && n->kind != ND_FUNC)
      /* gcc warns here; the attribute simply has no target */
      a.no_ret = 0;
    n->is_static = is_static;
    n->is_extern = is_extern;
    n->is_inline = is_inline;
    n->is_noreturn = is_noreturn || a.no_ret;
    n->is_thread = is_thread;
    n->align = alignas;
    if (a.align > n->align)
      n->align = a.align;
    if (n->kind != ND_FUNC && (is_inline || is_noreturn))
      error_at(tok->loc, "%s in declaration of non-function '%s'",
               is_inline ? "inline" : "_Noreturn", n->name ? n->name : "");
    if (n->kind == ND_FUNC && alignas)
      error_at(tok->loc, "alignment specified for function '%s'", n->name);
    if (n->kind == ND_FUNC && is_thread)
      error_at(tok->loc, "'%s': thread-local storage applies only to objects",
               n->name ? n->name : "");
    if (!n->name && n->kind == ND_DECL) {
      /* a type-only declaration ("struct point {...};") carries
       * no storage, just a tag definition */
      expect_punct(";");
      return NULL;
    }
    /* a function definition: the body comes after any post-
     * declarator attribute */
    if (n->kind == ND_FUNC && is_punct("{"))
      n->body = parse_function_body(t, n->name);
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
  register_typedef("va_list", builtin_va_list_type());
  return parse_program();
}