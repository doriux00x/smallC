/* GNU builtins: __builtin_expect (value with its hint dropped),
 * __builtin_constant_p (constness query, argument never evaluated),
 * __builtin_types_compatible_p (two type names, no decay, top-level
 * qualifiers ignored), __builtin_choose_expr (constant-condition
 * selection where only the chosen arm compiles),
 * __builtin_unreachable (a no-op marker), and __builtin_offsetof
 * (a member's byte offset, folded to a constant like gcc's) */

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define TCP(a, b) __builtin_types_compatible_p(a, b)
#define offsetof(t, m) __builtin_offsetof(t, m)

int side;

int bump(void) { side++; return 7; }

enum { ECON = 9 };

struct Point { int x; int y; };

struct Inner { char a; short b; };    /* a:0, b:2 */
struct Pair { int p; short q; };      /* p:0, q:4 */
struct Outer {                        /* the 32-bit layout */
  char c0;                            /* 0 */
  struct Inner in;                    /* 2, size 4 */
  char c1;                            /* 6 */
  int arr[5];                         /* 8 */
  char tail[3];                       /* 28 */
  struct Pair pr[3];                  /* 32 */
};                                    /* size 56 */
union U { char c; int i; short s[4]; };

int main(void) {
  /* __builtin_expect: the value of the first operand, hint ignored */
  if (__builtin_expect(1, 1) != 1) return 1;
  if (__builtin_expect(0, 0) != 0) return 2;
  if (__builtin_expect(3 + 4, 0) != 7) return 3;
  if (likely(1) != 1 || unlikely(0) != 0) return 4;
  if (likely(0) != 0 || unlikely(1) != 1) return 5;

  /* the first operand evaluates exactly once, the hint never does */
  side = 0;
  if (__builtin_expect(bump(), 1) != 7 || side != 1) return 6;
  if (__builtin_expect(bump(), 0) != 7 || side != 2) return 7;

  /* __builtin_constant_p: constness, not value, and no evaluation */
  if (__builtin_constant_p(42) != 1) return 8;
  if (__builtin_constant_p(3 + 4 * 5) != 1) return 9;
  if (__builtin_constant_p(sizeof(int)) != 1) return 10;
  if (__builtin_constant_p(sizeof(struct Point)) != 1) return 11;
  if (__builtin_constant_p(_Alignof(long)) != 1) return 12;
  if (__builtin_constant_p(ECON) != 1) return 13;
  if (__builtin_constant_p("abc") != 1) return 14;
  side = 0;
  if (__builtin_constant_p(side) != 0) return 15;
  if (__builtin_constant_p(bump()) != 0) return 16;
  if (__builtin_constant_p(bump()) != 0 || side != 0) return 17;

  /* __builtin_types_compatible_p: qualifiers at the top are
   * ignored, nested ones matter, no array/function decay */
  if (TCP(int, const int) != 1) return 18;
  if (TCP(int, const volatile int) != 1) return 19;
  if (TCP(int, unsigned int) != 0) return 20;
  if (TCP(const int *, int *) != 0) return 21;
  if (TCP(int *, int *) != 1) return 22;
  if (TCP(int[5], int[5]) != 1) return 23;
  if (TCP(int[3], int[5]) != 0) return 24;
  if (TCP(int[], int[5]) != 1) return 25;
  if (TCP(int[], int[]) != 1) return 26;
  if (TCP(int *, int[5]) != 0) return 27;
  if (TCP(long int, long) != 1) return 28;
  if (TCP(long, long long) != 0) return 29;
  if (TCP(int, long long) != 0) return 30;
  if (TCP(char, unsigned char) != 0) return 31;
  if (TCP(float, double) != 0) return 32;
  if (TCP(int(), int (*)(void)) != 0) return 33;
  if (TCP(struct Point, struct Point) != 1) return 34;
  if (TCP(struct Point, int) != 0) return 35;
  if (TCP(short, short int) != 1) return 36;

  /* __builtin_choose_expr: the constant condition picks the arm,
   * and only the chosen arm is compiled at all */
  if (__builtin_choose_expr(1, 5, 6) != 5) return 37;
  if (__builtin_choose_expr(0, 5, 6) != 6) return 38;
  if (__builtin_choose_expr(2 > 1, 11, 12) != 11) return 39;
  if (__builtin_choose_expr(2 > 3, 11, 12) != 12) return 40;
  if (__builtin_choose_expr(sizeof(int) == 4, 13, 14) != 13) return 41;
  if (__builtin_choose_expr(!__builtin_constant_p(1), 1, 2) != 2) return 42;

  /* the loser is parsed but never resolved: an undeclared name there
   * is not an error, and its side effects never run */
  side = 0;
  if (__builtin_choose_expr(1, ++side, undeclared_loser) != 1) return 43;
  if (side != 1) return 44;
  if (__builtin_choose_expr(0, undeclared_loser_2, side++) != 1) return 45;
  if (side != 2) return 46;

  /* the chosen arm's type is the selection's type */
  if (__builtin_choose_expr(1, 5, 6.5) + 0.0 != 5.0) return 47;
  if (_Generic((__builtin_choose_expr(0, 5l, 4.5)),
               double: 1, default: 2) != 1) return 48;

  /* __builtin_unreachable: a no-op that is legal anywhere */
  __builtin_unreachable();
  {
    int x = (__builtin_unreachable(), 3);
    if (x != 3) return 49;
  }
  switch (2) {
    case 1: __builtin_unreachable();
    case 2: break;
    default: __builtin_unreachable();
  }

  /* the real-world idiom: a dispatch on constness like the glibc
   * fortify macros */
  {
    char buf[8];
    if (__builtin_choose_expr(__builtin_constant_p("lit"), 1, 0) != 1)
      return 50;
    if (__builtin_choose_expr(__builtin_constant_p(buf), 1, 0) != 0)
      return 51;
    buf[0] = 'x';
    if (buf[0] != 'x') return 52;
  }

  /* __builtin_offsetof: member byte offsets, constant-folded */
  if (__builtin_offsetof(struct Point, y) != 4) return 53;
  if (__builtin_offsetof(struct Point, x) != 0) return 54;

  /* .name steps walk into nested structs */
  if (__builtin_offsetof(struct Outer, c0) != 0) return 55;
  if (__builtin_offsetof(struct Outer, in) != 2) return 56;
  if (__builtin_offsetof(struct Outer, in.a) != 2) return 57;
  if (__builtin_offsetof(struct Outer, in.b) != 4) return 58;
  if (__builtin_offsetof(struct Outer, c1) != 6) return 59;
  if (__builtin_offsetof(struct Outer, arr) != 8) return 60;

  /* [const] subscripts step through arrays */
  if (__builtin_offsetof(struct Outer, arr[0]) != 8) return 61;
  if (__builtin_offsetof(struct Outer, arr[3]) != 20) return 62;
  if (__builtin_offsetof(struct Outer, tail[1]) != 29) return 63;
  if (__builtin_offsetof(struct Outer, pr[2].q) != 52) return 64;
  if (__builtin_offsetof(struct Outer, pr[1].p) != 40) return 65;

  /* the index is any constant expression, even a sizeof */
  if (__builtin_offsetof(struct Outer, arr[sizeof(int)]) != 24) return 66;
  if (__builtin_offsetof(struct Outer, arr[3 * 2 - 5]) != 12) return 67;

  /* a union has every member at 0, subscripts included */
  if (__builtin_offsetof(union U, i) != 0) return 68;
  if (__builtin_offsetof(union U, c) != 0) return 69;
  if (__builtin_offsetof(union U, s[2]) != 4) return 70;

  /* the stddef.h idiom: the result lands in global and enum
   * initializers, all of it decided at compile time */
  {
    static int tab[offsetof(struct Outer, pr[2].q)];
    enum { QOFF = __builtin_offsetof(struct Outer, pr[2].q) };
    if (sizeof(tab) != 52 * sizeof(int)) return 71;
    if (QOFF != 52) return 72;
  }

  /* the container_of workhorse: member address minus its offset */
  {
    struct Outer o;
    int *arrp = &o.arr[0];
    struct Outer *back = (struct Outer *)((char *)arrp -
                                          __builtin_offsetof(struct Outer, arr));
    if (back != &o) return 73;
    o.c1 = 5;
    if (back->c1 != 5) return 74;
  }

  printf("runbuiltin ok\n");
  return 0;
}