int fib(int n) {
  if (n == 0)
    return 0;
  if (n == 1)
    return 1;
  return fib(n - 1) + fib(n - 2);
}

int main() {
  if (fib(10) != 55) return 1;

  char c = 'a';
  c++;
  if (c != 'b') return 2;

  char *p = "hello";
  int len = 0;
  while (p[len])
    len++;
  if (len != 5) return 3;

  if (sizeof("hello") != 6) return 4;

  unsigned char u = 200;
  if (u + 100 != 300) return 5;
  if (u < 150) return 6;

  printf("run3: fib=%d\n", fib(10));
  return 0;
}