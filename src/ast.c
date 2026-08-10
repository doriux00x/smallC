#include "ast.h"
#include "util.h"

#include <stdlib.h>

static int type_size(Type *t) {
  switch (t->kind) {
    case TY_VOID:   return 0;
    case TY_CHAR:   return 1;
    case TY_SHORT:  return 2;
    case TY_INT:    return 4;
    case TY_LONG:   return t->is_longlong ? 8 : 4;
    case TY_FLOAT:  return 4;
    case TY_DOUBLE: return 8;
    case TY_PTR:    return 4;   /* FIXME: default 32-bit target, -m16 later */
    case TY_ARRAY:  return type_size(t->base) * t->array_len;
  }
  return 0;
}

Type *type_new(TypeKind k) {
  Type *t = xmalloc(sizeof(Type));
  t->kind = k;
  t->size = type_size(t);
  return t;
}

Type *ptr_to(Type *base) {
  Type *t = xmalloc(sizeof(Type));
  t->kind = TY_PTR;
  t->base = base;
  t->size = type_size(t);
  return t;
}

Type *array_of(Type *base, int len) {
  Type *t = xmalloc(sizeof(Type));
  t->kind = TY_ARRAY;
  t->base = base;
  t->array_len = len;
  t->size = type_size(t);
  return t;
}
