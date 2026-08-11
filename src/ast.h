#ifndef AST_H
#define AST_H

typedef struct Node Node;
typedef struct Obj Obj;

typedef enum {
  TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG,
  TY_FLOAT, TY_DOUBLE, TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT,
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
  int size;          /* bytes per target ABI */
  int align;         /* alignment, same ABI */
  int array_len;     /* TY_ARRAY only */
  Type *base;        /* pointee / element type */
  Type *ret;         /* TY_FUNC return type */
  Node *params;      /* TY_FUNC params, ND_DECL nodes linked by next */
  Member *members;   /* TY_STRUCT */
  Type *mark_prev;   /* cycle guard for the -a dump & struct member walks */
};

Type *type_new(TypeKind k);
Type *ptr_to(Type *base);
Type *array_of(Type *base, int len);
Type *func_type(Type *ret);
Type *struct_type(void);
void layout_struct(Type *t);
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
  ND_RETURN,         /* lhs or NULL */
  ND_BREAK,
  ND_CONTINUE,
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

struct Node {
  NodeKind kind;
  Type *type;        /* ND_DECL only; expr nodes get typed in the sema pass */
  Node *lhs, *rhs;
  Node *cond, *then, *els;     /* ND_COND / ND_IF / ND_WHILE / ND_FOR */
  Node *args;                  /* ND_CALL, linked by next */
  Node *body;                  /* ND_FUNC body / ND_BLOCK children */
  Node *init;                  /* ND_DECL / ND_FOR init */
  Node *inc;                   /* ND_FOR */
  Node *next;
  char *name;                  /* identifier */
  char *str;                   /* ND_STR decoded contents */
  int str_len;
  int val;                     /* ND_NUM, int value */
  int is_float;                /* ND_NUM: double value in fval */
  double fval;                 /* ND_NUM, float value */
  int op;                      /* operator code */
  int is_pntr;                 /* ND_MEMBER: "->" vs "." */
  int is_prefix;               /* ND_UNARY ++/--: prefix vs postfix */
  Obj *var;                    /* resolved symbol, ND_VAR / ND_STR */
  Type *targ;                  /* ND_SIZEOF / ND_CAST type operand */
};

#endif