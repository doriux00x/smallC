/* const: qualifier on scalars, pointers (pointee and pointer),
 * params, globals, arrays, structs, unions, typedef names; reads
 * stay legal. */

const int g_pi = 3;
int const g_after = 5;
const char g_letter = 'a';
const int g_arr[3] = { 1, 2, 3 };

typedef const int cint;
cint g_c = 7;

struct S { const int a; int b; };
const struct S g_s = { 1, 2 };

const char *g_str = "hi";

void consume(const int *vals, int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += vals[i];
  int *p = (int *)&s;
  *p = s;   /* s is writable, vals is not */
}

int main(void) {
  int check = 0;

  /* const treats the object as read-only, values are unchanged */
  check += (g_pi == 3 && g_after == 5 && g_letter == 'a');
  check += (g_arr[0] + g_arr[1] + g_arr[2] == 6);
  check += (g_c == 7);
  check += (g_s.a == 1 && g_s.b == 2);
  check += (g_str[0] == 'h' && g_str[1] == 'i');
  consume(g_arr, 3);

  const int x = 10;
  check += (x == 10);

  int const y = 11;
  check += (y == 11);

  unsigned const u = 12;
  check += (u == 12);

  const char *s = "ab";
  check += (s[1] == 'b');

  char buf[4] = "xy";
  const char *bs = buf;
  check += (bs[0] == 'x');

  int m = 5;
  int *const pm = &m;
  check += (*pm == 5);

  const int arr[2] = { 7, 8 };
  check += (arr[1] == 8);

  struct S st = { 1, 2 };
  check += (st.a == 1 && st.b == 2);

  union U { int i; const int ci; };
  union U myu = { 9 };
  check += (myu.i == 9);

  const union U cu = { 10 };
  check += (cu.ci == 10 || cu.i == 10);

  enum E { A1, B1 };
  const enum E ce = B1;
  check += (ce == B1);

  cint cc = 4;
  check += (cc == 4);

  int msum = 0;
  for (int i = 0; i < 3; i++)
    msum += g_arr[i];
  check += (msum == 6);

  if (check != 18)
    return check;
  printf("runconst ok\n");
  return 0;
}