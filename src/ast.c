#include "ast.h"
#include "util.h"

#include "libc.h"

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
    case TY_STRUCT:
    case TY_UNION:  return t->size;
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
    case TY_STRUCT:
    case TY_UNION:  return t->align;
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

Type *union_type(void) {
  Type *t = type_zalloc();
  t->kind = TY_UNION;
  t->size = 0;
  t->align = 1;
  return t;
}

void layout_struct(Type *t) {
  int off = 0;
  int max_align = 1;
  /* an open bit-field unit: bit-fields of the same base type pack
   * into it while they fit, then it is closed back into off */
  int unit_off = -1;
  int unit_bits = 0;
  int unit_size = 0;

  for (Member *m = t->members; m; m = m->next) {
    if (m->is_bitfield) {
      if (m->bit_width == 0) {
        /* the anonymous marker: force the next bit-field into a
         * fresh unit, aligned to the *next* member's type */
        if (unit_off >= 0)
          off = unit_off + unit_size;
        unit_off = -1;
        continue;
      }
      if (unit_off < 0 || m->type->size != unit_size ||
          m->bit_width > unit_size * 8 - unit_bits) {
        if (unit_off >= 0)
          off = unit_off + unit_size;
        int a = type_align(m->type);
        off = (off + a - 1) / a * a;
        unit_off = off;
        unit_bits = 0;
        unit_size = m->type->size;
      }
      m->offset = unit_off;
      m->bit_offset = unit_bits;
      unit_bits += m->bit_width;
      if (type_align(m->type) > max_align)
        max_align = type_align(m->type);
      continue;
    }
    if (unit_off >= 0) {
      /* a plain member closes the running bit-field unit */
      off = unit_off + unit_size;
      unit_off = -1;
      unit_bits = 0;
    }
    int a = type_align(m->type);
    off = (off + a - 1) / a * a;
    m->offset = off;
    off += m->type->size;
    if (a > max_align)
      max_align = a;
  }
  if (unit_off >= 0)
    off = unit_off + unit_size;
  t->align = max_align;
  t->size = (off + max_align - 1) / max_align * max_align;
}

/* every member sits at offset 0; size is the widest member, rounded
 * up to the union's alignment */
void layout_union(Type *t) {
  int max_align = 1;
  int max_size = 0;
  for (Member *m = t->members; m; m = m->next) {
    m->offset = 0;
    if (m->type->size > max_size)
      max_size = m->type->size;
    int a = type_align(m->type);
    if (a > max_align)
      max_align = a;
  }
  t->align = max_align;
  t->size = (max_size + max_align - 1) / max_align * max_align;
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

static CVal cv_int(int v)   { CVal c = {0, v, 0};    return c; }
static CVal cv_fp(double f) { CVal c = {1, 0, f};    return c; }

/* every node kind const_fold() can evaluate, i.e. an integer
 * constant expression with no variables or side effects */
int is_const_expr(Node *n) {
  switch (n->kind) {
    case ND_NUM:
    case ND_SIZEOF:
      return 1;
    case ND_CAST:
      return is_const_expr(n->lhs);
    case ND_UNARY:
    case ND_COND:
      return is_const_expr(n->lhs) &&
             (!n->rhs || is_const_expr(n->rhs)) &&
             (!n->cond || is_const_expr(n->cond)) &&
             (!n->els || is_const_expr(n->els));
    case ND_BIN:
      return is_const_expr(n->lhs) && is_const_expr(n->rhs);
    default:
      return 0;
  }
}

/* constant folding for case labels, bit-field widths and global
 * initializers. a folded value is either an integer or a double */
CVal const_fold(Node *n) {
  switch (n->kind) {
    case ND_NUM:
      if (n->is_float)
        return cv_fp(n->fval);
      return cv_int(n->val);
    case ND_SIZEOF:
      if (n->lhs)
        return cv_int(type_size(n->lhs->type));
      return cv_int(type_size(n->targ));
    case ND_CAST:
      if (n->targ->kind == TY_FLOAT || n->targ->kind == TY_DOUBLE) {
        CVal c = const_fold(n->lhs);
        return cv_fp(c.is_float ? c.fval : (double)c.val);
      }
      if (n->targ->kind == TY_PTR)
        error("unsupported global initializer");
      {
        CVal c = const_fold(n->lhs);
        return cv_int(c.is_float ? (int)c.fval : c.val);
      }
    case ND_UNARY:
      switch (n->op) {
        case '+': {
          CVal c = const_fold(n->lhs);
          return c;
        }
        case '-': {
          CVal c = const_fold(n->lhs);
          return c.is_float ? cv_fp(-c.fval) : cv_int(-c.val);
        }
        case '~': {
          CVal c = const_fold(n->lhs);
          if (c.is_float)
            error("invalid operands to binary operator");
          return cv_int(~c.val);
        }
        case '!': {
          CVal c = const_fold(n->lhs);
          return cv_int(c.is_float ? c.fval == 0 : !c.val);
        }
        default: error("unsupported global initializer");
      }
    case ND_BIN: {
      CVal l = const_fold(n->lhs);
      CVal r = const_fold(n->rhs);
      if (l.is_float || r.is_float) {
        if (n->op == '%' || n->op == '&' || n->op == '|' ||
            n->op == '^' || n->op == OP_SHL || n->op == OP_SHR)
          error("invalid operands to binary operator");
        double a = l.is_float ? l.fval : (double)l.val;
        double b = r.is_float ? r.fval : (double)r.val;
        switch (n->op) {
          case '+': return cv_fp(a + b);
          case '-': return cv_fp(a - b);
          case '*': return cv_fp(a * b);
          case '/':
            if (b == 0)
              error("division by zero in constant expression");
            return cv_fp(a / b);
          case OP_EQ:  return cv_int(a == b);
          case OP_NE:  return cv_int(a != b);
          case '<':  return cv_int(a < b);
          case '>':  return cv_int(a > b);
          case OP_LE: return cv_int(a <= b);
          case OP_GE: return cv_int(a >= b);
          case OP_LOGAND: return cv_int(a != 0 && b != 0);
          case OP_LOGOR:  return cv_int(a != 0 || b != 0);
          default: error("unsupported global initializer");
        }
      } else {
        int lv = l.val;
        int rv = r.val;
        switch (n->op) {
          case '+': return cv_int(lv + rv);
          case '-': return cv_int(lv - rv);
          case '*': return cv_int(lv * rv);
          case '/':
            if (rv == 0)
              error("division by zero in constant expression");
            return cv_int(lv / rv);
          case '%':
            if (rv == 0)
              error("division by zero in constant expression");
            return cv_int(lv % rv);
          case '&':  return cv_int(lv & rv);
          case '|':  return cv_int(lv | rv);
          case '^':  return cv_int(lv ^ rv);
          case OP_SHL: return cv_int(lv << rv);
          case OP_SHR: return cv_int(lv >> rv);
          case OP_EQ:  return cv_int(lv == rv);
          case OP_NE:  return cv_int(lv != rv);
          case '<':  return cv_int(lv < rv);
          case '>':  return cv_int(lv > rv);
          case OP_LE: return cv_int(lv <= rv);
          case OP_GE: return cv_int(lv >= rv);
          case OP_LOGAND: return cv_int(lv && rv);
          case OP_LOGOR:  return cv_int(lv || rv);
          default: error("unsupported global initializer");
        }
      }
    }
    case ND_COND: {
      CVal c = const_fold(n->cond);
      return const_fold(c.is_float ? (c.fval != 0 ? n->then : n->els) :
                                    (c.val ? n->then : n->els));
    }
    default:
      error("unsupported global initializer");
  }
  error("unsupported global initializer");
}
