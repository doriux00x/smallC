/* typeof / __typeof__ (GNU): the compile-time type of an expression,
   usable wherever a type name can appear. */

#define MAX(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })
#define MIN(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })

int core(void) { return 3; }
double fcore(void) { return 2.5; }

struct S { int n; double d; };
typedef struct S S;

int main(void) {
  /* the classic use: type-safe max/min without repeated evaluation */
  if (MAX(3, 7) != 7) return 1;
  if (MIN(3, 7) != 3) return 2;
  double x = 1.5, y = 4.25;
  if (MAX(x, y) != 4.25) return 3;
  if (MIN(x, y) != 1.5) return 4;
  long la = 9, lb = 2;
  if (MAX(la, lb) != 9) return 5;
  if (MAX(x, MIN(la, lb)) != 2.0) return 6;

  /* type form: typeof(int), typeof(struct S), typeof(typedef) */
  if (sizeof(typeof(int)) != 4) return 7;
  if (sizeof(typeof(struct S)) != 16) return 8;
  if (sizeof(typeof(S)) != 16) return 9;
  if (_Alignof(typeof(x)) != 8) return 10;
  if (sizeof(typeof(int *)) != 8) return 11;

  /* expression form over the parse-scope registry */
  if (sizeof(typeof(x)) != 8) return 12;
  if (sizeof(typeof(core())) != 4) return 13;
  if (sizeof(typeof(fcore())) != 8) return 14;
  if (sizeof(typeof(&core)) != 8) return 15;
  int arr[4];
  if (sizeof(typeof(arr)) != 16) return 16;
  if (sizeof(typeof("hi")) != 3) return 17;
  int *p = arr;
  if (sizeof(typeof(*p)) != 4) return 18;
  if (sizeof(typeof(p[2])) != 4) return 19;
  if (sizeof(typeof(&p)) != 8) return 20;
  if (sizeof(typeof((long)p)) != 8) return 21;
  if (sizeof(typeof(1.0f)) != 4) return 22;
  struct S sv = {3, 3.5};
  if (sizeof(typeof(sv.d)) != 8) return 23;
  S *sp = &sv;
  if (sizeof(typeof(sp->n)) != 4) return 24;
  if (sizeof(typeof(1 + 2L)) != 8) return 25;
  if (sizeof(typeof(x + y)) != 8) return 26;
  if (sizeof(typeof(1 ? 1 : 2L)) != 8) return 27;

  /* the __typeof__ spelling is the same keyword */
  if (sizeof(__typeof__(x)) != 8) return 28;
  if (sizeof(__typeof(x)) != 8) return 29;

  /* typeof in declarations and casts */
  typeof(sv) sv2 = sv;
  if (sv2.d != 3.5) return 30;
  if (sv2.n != 3) return 31;
  typeof(x) z = 9.5;
  if (z != 9.5) return 32;
  typeof(x) *pz = &z;
  if (*pz != 9.5) return 33;
  char ch = (typeof(ch))97;
  if (ch != 'a') return 34;
  long ll = (typeof(ll))MAX(x, y);
  if (ll != 4) return 35;
  double dd = (typeof(dd))9;
  if (dd != 9.0) return 36;

  /* a function type's size is a pointer via &, and typeof(core) is
   * the function type itself for a pointer-to-function variable */
  typeof(core) *fp = core;
  if ((*fp)() != 3) return 37;

  /* expression forms the control passes through keep their type:
   * declarations, casts and the operators select the C type */
  if (sizeof(typeof(_Generic(1L, long: 0, default: 0))) != 4) return 38;
  if (sizeof(typeof(sizeof(long))) != 8) return 39;
  if (sizeof(typeof(_Alignof(int))) != 8) return 40;
  if (sizeof(typeof(1 ? 0 : 0L)) != 8) return 41;

  printf("runtypeof ok\n");
  return 0;
}