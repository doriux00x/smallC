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

/* sizes for the x86-64 SysV target. the -m16/-m32 legacy targets will
 * need this table replaced wholesale, that's why it's one function */
int type_size(Type *t) {
  switch (t->kind) {
    case TY_VOID:   return 1;
    case TY_CHAR:   return 1;
    case TY_SHORT:  return 2;
    case TY_INT:    return 4;
    case TY_LONG:   return 8;
    case TY_FLOAT:  return 4;
    case TY_DOUBLE: return 8;
    case TY_PTR:    return 8;
    case TY_ARRAY:  return type_size(t->base) * t->array_len;
    case TY_FUNC:   return 0;   /* sizeof(func) illegal; designators decay */
    case TY_STRUCT: return t->size;
  }
  return 0;
}

/* SysV alignment: scalars align to their size, arrays to their
 * element, structs to their widest member */
static int type_align(Type *t) {
  switch (t->kind) {
    case TY_CHAR:   return 1;
    case TY_SHORT:  return 2;
    case TY_INT:
    case TY_FLOAT:  return 4;
    case TY_LONG:
    case TY_DOUBLE:
    case TY_PTR:    return 8;
    case TY_ARRAY:  return type_align(t->base);
    case TY_STRUCT: return t->align;
    default:        return 1;
  }
}

/* the tag slot of a struct definition; members get attached and
 * laid out once the closing brace is parsed, so the tag can be
 * referenced from inside its own body (struct Node *next) */
Type *struct_type(void) {
  Type *t = type_zalloc();
  t->kind = TY_STRUCT;
  t->size = 0;
  t->align = 1;
  return t;
}

void layout_struct(Type *t) {
  int off = 0;
  int max_align = 1;
  for (Member *m = t->members; m; m = m->next) {
    int a = type_align(m->type);
    off = (off + a - 1) / a * a;
    m->offset = off;
    off += m->type->size;
    if (a > max_align)
      max_align = a;
  }
  t->align = max_align;
  t->size = (off + max_align - 1) / max_align * max_align;
}

Type *type_new(TypeKind k) {
  Type *t = type_zalloc();
  t->kind = k;
  t->size = type_size(t);
  t->align = type_align(t);
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
