/* union: shared storage per member, size of the widest member,
 * first-member initializers, by-value params/returns/assignment,
 * members nested in structs. */

union word { int i; char b[4]; };
union big { double d; char c; long l; };
union tiny { char c; short s; };
union outer { int i; union word w; };

union word g_u = { 7 };
union big g_big;

union uv { int i; char c; };

union uv make(int x) {
  union uv u;
  u.i = x;
  return u;
}

int geti(union uv u) {
  return u.i;
}

int main(void) {
  int check = 0;

  check += (sizeof(union word) == 4 && sizeof(union big) == 8 &&
            sizeof(union tiny) == 2 && sizeof(union outer) == 4);

  union word w;
  w.i = 0x41424344;
  check += (w.b[0] == 0x44 && w.b[3] == 0x41);   /* little-endian */
  w.b[0] = 0x58;
  check += (w.i == 0x41424358);

  union word w2 = { 5 };
  check += (w2.i == 5 && w2.b[0] == 5);

  check += (g_u.i == 7 && g_u.b[0] == 7);

  g_big.l = 123456789;
  check += (g_big.l == 123456789 && g_big.c == 0x15);

  union big b = { 3.0 };
  check += (b.d == 3.0 && b.c == 0);

  union outer o;
  o.w.i = 9;
  check += (o.i == 9 && o.w.b[0] == 9);

  union word *p = &w2;
  p->i = 42;
  check += (w2.i == 42);

  union word arr[2];
  arr[0].i = 1;
  arr[1].i = 2;
  check += (arr[0].i + arr[1].i == 3 && sizeof(arr) == 8);

  union uv a = make(77);
  check += a.i == 77;

  union uv b2 = a;             /* whole-union assignment */
  check += b2.i == 77;

  check += geti(b2) == 77;

  union uv c;
  c = make(5);                 /* assign a call result */
  check += c.i == 5;

  struct wrap { union uv u; int n; } s;
  s.u.i = 9;
  s.n = 4;
  check += s.u.i == 9 && s.n == 4 && sizeof(s) == 8;

  if (check != 15)
    return check;
  printf("rununion ok\n");
  return 0;
}