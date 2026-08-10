#ifndef AST_H
#define AST_H

typedef enum {
  TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG,
  TY_FLOAT, TY_DOUBLE, TY_PTR, TY_ARRAY,
} TypeKind;

typedef struct Type Type;

struct Type {
  TypeKind kind;
  int is_unsigned;   /* signed/unsigned modifier */
  int is_longlong;   /* "long long" */
  int size;          /* bytes per target ABI */
  int array_len;     /* TY_ARRAY only */
  Type *base;        /* pointee / element type */
};

Type *type_new(TypeKind k);
Type *ptr_to(Type *base);
Type *array_of(Type *base, int len);

typedef enum {
  ND_DECL,           /* variable declaration */
} NodeKind;

typedef struct Node Node;

struct Node {
  NodeKind kind;
  char *name;        /* declared identifier */
  Type *type;
  Node *next;
};

#endif
