/* _Generic (C11 type-generic selection): pick an association by the
 * controlling expression's compile-time type, without evaluating it */

#define TAG(x) _Generic((x), int: 100, double: 200, char *: 300, default: 400)

int main(void) {
  /* the basic scalar associations */
  int i = 3;
  if (_Generic(i, int: 1, default: 2) != 1) return 1;
  char c = 4;
  if (_Generic(c, int: 1, char: 2, default: 3) != 2) return 2;
  long l = 5;
  if (_Generic(l, char: 1, int: 2, long: 3, default: 4) != 3) return 3;
  double d = 6.0;
  if (_Generic(d, float: 1, double: 2, default: 3) != 2) return 4;
  float f = 7.0;
  if (_Generic(f, float: 1, double: 2, default: 3) != 1) return 5;
  unsigned u = 8;
  if (_Generic(u, int: 1, unsigned: 2, default: 3) != 2) return 6;
  _Bool b = 1;
  if (_Generic(b, char: 1, _Bool: 2, default: 3) != 2) return 7;

  /* the default arm is only taken when nothing else matches */
  if (_Generic(i, long: 1, default: 2) != 2) return 8;
  if (_Generic(d, int: 1, default: 2) != 2) return 9;

  /* a const control expression still matches the plain association:
   * lvalue conversion drops the top-level qualifier */
  const int ci = 9;
  if (_Generic(ci, int: 1, default: 2) != 1) return 10;
  const char *cp = 0;
  if (_Generic(cp, char *: 1, int *: 2, default: 3) != 3) return 11;

  /* pointer associations are distinct, and pointee qualifiers count */
  int *ip = 0;
  if (_Generic(ip, long *: 1, int *: 2, default: 3) != 2) return 12;
  if (_Generic(ip, const int *: 1, int *: 2, default: 3) != 2) return 13;
  if (_Generic(ip, char *: 1, default: 2) != 2) return 14;
  const int *cip = 0;
  if (_Generic(cip, const int *: 1, int *: 2, default: 3) != 1) return 15;
  void *vp = 0;
  if (_Generic(vp, int *: 1, void *: 2, default: 3) != 2) return 16;

  /* arrays and functions decay to pointers; the control value may
   * itself be an array or a string literal */
  int arr[3];
  if (_Generic(arr, int *: 1, default: 2) != 1) return 17;
  if (_Generic("hi", char *: 1, char[3]: 2, default: 3) != 1) return 18;
  if (_Generic(&main, int (*)(void): 1, default: 2) != 1) return 19;

  /* the controlling expression is never evaluated */
  int x = 0;
  if (_Generic(x++, int: 7, default: 8) != 7) return 20;
  if (x != 0) return 21;

  /* only the matching arm is a candidate for evaluation */
  int side = 0;
  if (_Generic(i, int: (side = 1, 5), double: (side = 2, 6), default: 9) != 5)
    return 22;
  if (side != 1) return 23;

  /* the result keeps the arm's own type, no joining occurs */
  if (_Generic(i, int: 1.5, default: 2) != 1.5) return 24;
  if (_Generic(d, int: 1, default: 2.5) != 2.5) return 25;

  /* the selection is an lvalue when the chosen arm is */
  int y = 0;
  _Generic(y, int: y) = 42;
  if (y != 42) return 26;
  _Generic(i, int: y, long: y)++;
  if (y != 43) return 27;

  /* the canonical use: a type-dispatch macro */
  char *s = 0;
  if (TAG(i) != 100) return 28;
  if (TAG(d) != 200) return 29;
  if (TAG(s) != 300) return 30;
  if (TAG(l) != 400) return 31;
  /* pointee qualifiers survive, so a const-qualified pointer falls
   * through to the default arm */
  if (TAG(cp) != 400) return 32;

  /* nested selections, and struct/union tag types */
  if (_Generic(_Generic(i, int: 1.0f), float: 10, double: 20, default: 30) != 10)
    return 33;
  struct S1 { int a; };
  struct S2 { int b; };
  struct S1 s1;
  if (_Generic(s1, struct S2: 1, struct S1: 2, default: 3) != 2) return 34;
  union U1 { int a; };
  union U1 u1;
  if (_Generic(u1, struct S1: 1, union U1: 2, default: 3) != 2) return 35;

  printf("rungeneric ok\n");
  return 0;
}