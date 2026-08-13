#ifndef AST_H
#define AST_H

typedef struct Node Node;
typedef struct Obj Obj;

typedef enum {
  TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG,
  TY_FLOAT, TY_DOUBLE, TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT, TY_UNION,
} TypeKind;

typedef struct Type Type;
typedef struct Member Member;

struct Member {
  Member *next;
  char *name;
  Type *type;
  int offset;          /* filled in by layout_struct() */
};

struct Type {
  TypeKind kind;
  int is_unsigned;   /* signed/unsigned modifier */
  int is_longlong;   /* "long long" */
  int is_const;      /* const qualifier: no writes allowed */
  int is_volatile;   /* volatile qualifier: accepted; the backend never
                        elides or reorders memory loads anyway */
  int is_bool;       /* _Bool: 1-byte object, stored value is 0 or 1 */
  int size;          /* bytes per target ABI */
  int align;         /* alignment, same ABI */
  int array_len;     /* TY_ARRAY only */
  Type *base;        /* pointee / element type */
  Type *ret;         /* TY_FUNC return type */
  Node *params;      /* TY_FUNC params, ND_DECL nodes linked by next */
  int is_variadic;   /* TY_FUNC: "..." params; stdarg machinery */
  Member *members;   /* TY_STRUCT / TY_UNION */
  Type *mark_prev;   /* cycle guard for the -a dump & struct member walks */
};

Type *type_new(TypeKind k);
Type *ptr_to(Type *base);
Type *array_of(Type *base, int len);
Type *func_type(Type *ret);
Type *struct_type(void);
Type *union_type(void);
void layout_struct(Type *t);
void layout_union(Type *t);
int type_size(Type *t);

typedef enum {
  ND_DECL,           /* variable declaration */
  ND_FUNC,           /* function, body is NULL for a prototype */
  ND_BLOCK,          /* { ... }, children chained in body */
  ND_EXPR_STMT,      /* expression statement */
  ND_IF,             /* cond / then / els */
  ND_WHILE,          /* cond / then */
  ND_DO_WHILE,       /* then / cond */
  ND_FOR,            /* init / cond / inc / then */
  ND_SWITCH,         /* cond / body; labels stay in the body tree */
  ND_CASE,           /* case label: lhs is the value, NULL for default */
  ND_RETURN,         /* lhs or NULL */
  ND_BREAK,
  ND_CONTINUE,
  ND_GOTO,           /* goto name; jumps to the label in this function */
  ND_LABEL,          /* name : stmt; body is the labelled statement */
  ND_NUM,            /* integer literal */
  ND_STR,            /* string literal */
  ND_VAR,            /* variable reference */
  ND_ASSIGN,         /* = and compound assignments, op */
  ND_BIN,            /* binary operator, op */
  ND_UNARY,          /* unary operator, op */
  ND_COND,           /* a ? b : c */
  ND_CALL,           /* function call, args */
  ND_INDEX,          /* lhs[rhs] */
  ND_MEMBER,         /* lhs.member or lhs->member, is_pntr */
  ND_SIZEOF,         /* sizeof expr (lhs) or sizeof type (targ) */
  ND_CAST,           /* value conversion, target type in targ */
  ND_INIT_LIST,      /* { e1, e2, ... } initializer, children in elems */
  ND_VA_START,       /* va_start(ap, last); lhs is ap, va[] the counts */
  ND_VA_ARG,         /* va_arg(ap, T); lhs is ap, targ the target type */
  ND_COMP_LIT,       /* C99 (T){...}; targ is T, elems the brace list */
} NodeKind;

/* operator codes for ND_BIN/ND_UNARY/ND_ASSIGN.
 * single chars (+, -, *, /, %, &, |, ^, ~, !, <, >, =) are stored as
 * their ASCII value, anything longer gets one of these */
enum {
  OP_SHL = 256, OP_SHR,
  OP_EQ, OP_NE, OP_LE, OP_GE,
  OP_LOGAND, OP_LOGOR,
  OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN, OP_DIV_ASSIGN,
  OP_MOD_ASSIGN, OP_SHL_ASSIGN, OP_SHR_ASSIGN, OP_AND_ASSIGN,
  OP_OR_ASSIGN, OP_XOR_ASSIGN,
  OP_INC, OP_DEC,
};

/* one scalar slot of a flattened brace initializer; expr is NULL for
 * a zero-filled slot */
typedef struct {
  Type *ty;
  int offset;
  Node *expr;
} Init;

struct Node {
  NodeKind kind;
  Type *type;        /* ND_DECL only; expr nodes get typed in the sema pass */
  Node *lhs, *rhs;
  Node *cond, *then, *els;     /* ND_COND / ND_IF / ND_WHILE / ND_FOR */
  Node *args;                  /* ND_CALL, linked by next */
  Node *body;                  /* ND_FUNC body / ND_BLOCK children */
  Node *init;                  /* ND_DECL / ND_FOR init */
  Node *elems;                 /* ND_INIT_LIST, linked by next */
  Node *inc;                   /* ND_FOR */
  Node *next;
  int label;                   /* ND_CASE: jump label, filled by codegen */
  char *name;                  /* identifier */
  char *str;                   /* ND_STR decoded contents */
  int str_len;
  int val;                     /* ND_NUM, int value */
  int is_float;                /* ND_NUM: floating value in fval */
  int is_f;                    /* ND_NUM: the f/F suffix, a float literal */
  int is_unsigned;             /* ND_NUM: hex value past INT_MAX */
  double fval;                 /* ND_NUM, float value */
  int op;                      /* operator code */
  int is_pntr;                 /* ND_MEMBER: "->" vs "." */
  int is_prefix;               /* ND_UNARY ++/--: prefix vs postfix */
  int is_static;               /* ND_DECL / ND_FUNC: static storage */
  int is_extern;               /* ND_DECL / ND_FUNC: extern class */
  Obj *var;                    /* resolved symbol, ND_VAR / ND_STR;
                                  ND_CALL: hidden struct return buffer;
                                  ND_RETURN: the function's "~ret" param */
  Type *targ;                  /* ND_SIZEOF / ND_CAST type operand */
  int va[4];                   /* ND_VA_START: gp_off, fp_off, overflow
                                  and reg-save rbp offsets */
  Init *inits;                 /* ND_DECL: flattened initializer leaves */
  int init_n;
};

#endif