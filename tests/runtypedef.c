/* typedef: named type aliases. exercises scalar, pointer, array,
 * function-pointer and struct typedefs, plus modifiers on aliases,
 * alias-of-alias, shadowing in inner blocks and typedef'd tags. */

typedef int myint;
typedef unsigned long ulong;
typedef char *string;
typedef int intarr[3];
typedef int (*fnp)(int);
typedef struct pt { int x, y; } Pt;
typedef void (*vn_t)(void);

typedef myint alias2;

myint add(myint a, myint b) { return a + b; }

int sum(int *a) { return a[0] + a[1] + a[2]; }

int apply(fnp f, int n) { return f(n); }
int sq(int n) { return n * n; }

void nothing(void) { }
vn_t getvt(void) { return nothing; }

Pt mk(int x, int y) { Pt p; p.x = x; p.y = y; return p; }

int main(void) {
  int check = 0;

  myint a = 4, b = 5;
  check += add(a, b) == 9;

  ulong u = 4000000000;
  unsigned long u2 = u;
  ulong u3 = u2;
  check += sizeof(u) == 8 && u3 == 4000000000;

  string s = "hello";
  char *c = s;
  int n = 0;
  while (*c) { n++; c++; }
  check += n == 5;

  intarr v = {1, 2, 3};
  check += sum(v) == 6 && v[2] == 3 && sizeof(v) == 12;
  check += sizeof(intarr) == 12;

  fnp f = sq;
  check += apply(f, 7) == 49;
  fnp g = &sq;
  check += g(5) == 25;

  Pt p = mk(3, 9);
  check += p.x == 3 && p.y == 9 && sizeof(Pt) == 8;
  Pt *pp = &p;
  pp->x = 1;
  check += p.x == 1;

  vn_t f2 = getvt();
  f2();

  alias2 c2 = add(1, 2);
  check += c2 == 3;

  typedef int T;
  T t1 = 2;
  {
    typedef char T;
    T t2 = 'x';
    check += t2 == 'x';
  }
  check += t1 == 2;

  unsigned myint um = 12;
  check += um == 12;

  typedef int I;
  I i = 3;
  unsigned I ui = 7;
  check += i + ui == 10;

  if (check != 14)
    return check;
  printf("runtypedef ok\n");
  return 0;
}