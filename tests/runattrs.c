/* GNU header vocabulary: __attribute__((...)) with real packed /
 * aligned / noreturn semantics, the double-underscored keyword
 * aliases (__inline__, __signed__, __const, __restrict, ...),
 * __extension__ as a pedantic no-op, and the __func__ / __FUNCTION__
 * predefined identifiers */

struct __attribute__((packed)) P { char a; int b; short c; };
struct Q { char a; int b; short c; };

struct __attribute__((aligned(16))) R { char a; int b; };

struct After { char c; int n; } __attribute__((packed));

/**************** tips for checking member offsets without offsetof */

union Probe { struct P p; char bytes[16]; };
union ProbeAfter { struct After a; char bytes[16]; };

static __inline__ int twice(int x) { return x * 2; }

typedef __signed__ char sc_t;

__extension__ typedef long long big_t;

int guard(int n) { return __extension__ ({ n; }) * 2; }

int param_attr(int x __attribute__((unused))) { return 7; }

__attribute__((warn_unused_result)) int demanded(void) { return 3; }

const char *who(void) { return __func__; }
const char *also(void) { return __FUNCTION__; }

int __alignof_of_int(void);

int main(void) {
  /* packed drops padding and the struct's alignment, gcc's rule */
  if (sizeof(struct P) != 7) return 1;
  if (sizeof(struct Q) != 12) return 2;
  if ((long)&((union Probe *)0)->p.b != 1) return 3;
  if ((long)&((union Probe *)0)->p.c != 5) return 4;

  /* the attribute after the closing brace addresses the same tag */
  if (sizeof(struct After) != 5) return 5;
  if ((long)&((union ProbeAfter *)0)->a.n != 1) return 6;

  /* aligned on a struct type changes its alignment and size */
  if (sizeof(struct R) != 16) return 7;

  /* aligned on a declaration puts the object at the promise */
  __attribute__((aligned(16))) int x;
  if ((unsigned long)&x % 16 != 0) return 8;

  /* the aliases are the same keywords */
  if (twice(3) != 6) return 9;
  sc_t c = 1;
  if (c != 1) return 10;
  if (sizeof(big_t) != 8) return 11;
  if (__alignof_of_int() != 4) return 12;

  /* the attribute zoo parses and fades */
  if (demanded() != 3) return 13;
  if (param_attr(1) != 7) return 14;
  if (guard(4) != 8) return 15;

  /* __func__ / __FUNCTION__: a per-function static const char[] */
  if (who()[0] != 'w' || who()[1] != 'h' || who()[2] != 'o')
    return 16;
  if (who()[3] != 0) return 17;
  if (sizeof(__func__) != 5) return 18;
  if (__func__[0] != 'm') return 19;
  if (also()[1] != 'l') return 20;
  if (who() != who()) return 21;
  return 0;
}

int __alignof_of_int(void) { return __alignof(int); }