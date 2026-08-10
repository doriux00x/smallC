#include "ast.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* zeroed, so base/ret/params never hold garbage the walkers might chase */
static Type *type_zalloc(void) {
  Type *t = xmalloc(sizeof(Type));
  memset(t, 0, sizeof(Type));
  return t;
}

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
    case TY_FUNC:   return 0;   /* sizeof(func) illegal; designators decay */
  }
  return 0;
}

Type *type_new(TypeKind k) {
  Type *t = type_zalloc();
  t->kind = k;
  t->size = type_size(t);
  return t;
}

Type *ptr_to(Type *base) {
  Type *t = type_zalloc();
  t->kind = TY_PTR;
  t->base = base;
  t->size = type_size(t);
  return t;
}

Type *array_of(Type *base, int len) {
  Type *t = type_zalloc();
  t->kind = TY_ARRAY;
  t->base = base;
  t->array_len = len;
  t->size = type_size(t);
  return t;
}

Type *func_type(Type *ret) {
  Type *t = type_zalloc();
  t->kind = TY_FUNC;
  t->ret = ret;
  t->size = 0;
  return t;
}
