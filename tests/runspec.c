/* the C99/C11 specifier keywords: inline and _Noreturn on functions,
 * restrict as a type qualifier wherever const/volatile are legal */

static inline int add(int a, int b) { return a + b; }
static inline int mul(int a, int b) { return a * b; }

/* restrict appears on parameter pointees and on pointer-to-pointer */
int dot(int *restrict p, const int *restrict q, int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += p[i] * q[i];
  return s;
}
void swp(int *restrict a, int *restrict b) {
  int t = *a;
  *a = *b;
  *b = t;
}

/* a _Noreturn function never returns, so it must not contain a
 * return statement; calling one is just a call */
_Noreturn void spin(int seed) { for (;;) { } }

/* the specifiers compose in any order at top level */
_Noreturn static inline void unused(int k) { while (k) { } }

int main(void) {
  if (add(2, 3) != 5) return 1;
  if (mul(4, 5) != 20) return 2;

  int a[3] = {1, 2, 3};
  int b[3] = {4, 5, 6};
  if (dot(a, b, 3) != 32) return 3;

  swp(&a[0], &a[2]);
  if (a[0] != 3 || a[2] != 1) return 4;

  /* a restrict local pointer is accepted; the keyword is a hint at
   * best, the program's behavior is untouched */
  {
    int x[2] = {10, 20};
    int *restrict r = x;
    if (r[1] != 20) return 5;
  }

  /* a noreturn call can be gated; the call and what follows are
   * still compiled (no reachability analysis, so the code after it
   * is never actually reached here) */
  if (0) spin(2);
  (void)unused;

  printf("runspec ok\n");
  return 0;
}