int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int apply(int (*fp)(int, int), int a, int b) {
  return fp(a, b);
}

int main() {
  int (*fp)(int, int) = add;
  if (fp(20, 5) != 25) return 1;
  if ((*fp)(20, 5) != 25) return 2;
  if (apply(sub, 20, 5) != 15) return 3;
  fp = sub;
  if (apply(fp, 10, 3) != 7) return 4;
  if (fp != sub) return 5;
  if (&sub != fp) return 6;
  printf("fptr ok\n");
  return 0;
}