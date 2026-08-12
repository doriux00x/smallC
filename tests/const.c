/* dump mode: const front-end coverage */
const int g_a = 1;
int const g_b = 2;
const char *g_p = "ab";
char *const g_q = 0;
const int g_arr[2] = { 1, 2 };
const unsigned long g_ul = 3;

typedef const int cint;
cint g_c = 4;
typedef int *ip;
ip const g_ip = 0;

struct S { const int x; int y; };
const struct S g_s = { 1, 2 };
union U { const int i; char c; };
const union U g_u = { 5 };
enum E { A, B };
const enum E g_e = A;

int f(const int n, const char *s) {
  const int local = n;
  int const other = 2;
  const int *p = &local;
  int *const pp = &other;
  const cint cc = 6;
  const struct S st = { 1, 2 };
  unsigned const u = 3;
  return local + other + *p + *pp + cc + st.x + u;
}