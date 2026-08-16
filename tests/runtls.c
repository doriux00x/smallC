/* thread-local storage: _Thread_local / __thread as a storage class,
 * data in .tdata/.tbss, addressed through %fs:0 + @tpoff (each
 * thread sees its own copy; single-threaded here, so the values and
 * addresses are what gcc agrees on) */

int printf(const char *fmt, ...);

__thread int t1 = 7;
_Thread_local long t2;
static __thread char tc = 3;

extern __thread int t1;
extern _Thread_local long t2;

__thread int arr[3] = { 10, 20, 30 };

struct Pair { int a; long b; };
__thread struct Pair pair = { 1, 2 };

int plain_global = 100;

static int add_to_t1(int x) {
  t1 += x;
  return t1;
}

/* a block-scope thread-local static initialized once, kept across
 * calls to this function */
static int next_count(void) {
  static __thread int count = 1;
  count += 3;
  return count;
}

int main(void) {
  /* initial values, and access from another function */
  if (t1 != 7 || t2 != 0 || tc != 3) return 1;
  if (add_to_t1(5) != 12) return 2;
  if (t1 != 12) return 3;

  /* a block-scope thread-local static keeps its value across calls */
  if (next_count() != 4) return 4;
  if (next_count() != 7) return 5;
  if (t1 != 12) return 6;

  /* an inner block can shadow a thread-local static; the outer one
   * keeps its own copy (and its own @tpoff) untouched */
  {
    static __thread int count = 1;
    count += 3;
    if (count != 4) return 7;
    {
      static __thread int count = 2;
      count += 5;
      if (count != 7) return 8;
      t1 += count;
    }
    if (count != 4) return 9;
  }
  if (t1 != 19) return 10;

  /* ordinary statements can read and write through the addresses */
  t2 = 4;
  if (t2 != 4) return 11;
  tc = 9;
  if (tc != 9) return 12;

  /* the addresses are stable and distinct -- between TLS objects and
   * from plain globals (each TLS slot has its own @tpoff) */
  if (&t1 == (int *)&t2) return 13;
  if (&t2 == (long *)&tc) return 14;
  {
    int *p = &t1;
    if (p != &t1) return 15;
  }

  /* arrays and structs in TLS keep their layout and contents */
  if (arr[0] != 10 || arr[1] != 20 || arr[2] != 30) return 16;
  if (sizeof(arr) != sizeof(int) * 3) return 17;
  arr[1] = 21;
  if (arr[1] != 21) return 18;
  if (pair.a != 1 || pair.b != 2) return 19;
  if (sizeof(pair) != sizeof(struct Pair)) return 20;

  /* sizeof and _Alignof see the ordinary type */
  if (sizeof(t1) != sizeof(int)) return 21;
  if (_Alignof(t2) != _Alignof(long)) return 22;

  /* a TLS object can hide behind a pointer like anything else */
  {
    struct Pair *pp = &pair;
    pp->a = 3;
    if (pair.a != 3) return 23;
  }

  /* the extern + definition form binds in one TU */
  t1 = 0;
  if (t1 != 0) return 24;

  printf("runtls ok\n");
  return 0;
}