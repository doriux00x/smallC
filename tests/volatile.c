volatile int gv = 5;
const volatile int gcv = 9;

volatile int counter(volatile int x) {
  return x * 2;
}

int main() {
  volatile int v = 0;
  v = 7;
  int volatile tv = 3;
  volatile long vl = 5;
  volatile int *p = &v;
  int *volatile q = &v;
  return gv + gcv + v + tv + vl + counter(p != q ? 0 : 14);
}