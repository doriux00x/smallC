int add(int a, int b) {
  return a + b;
}

int sub(int a, int b) {
  return a - b;
}

int main() {
  int x = 10;
  int y = 3;

  if (x + y != 13) return 1;
  if (x - y != 7) return 2;
  if (x * y != 30) return 3;
  if (x / y != 3) return 4;
  if (x % y != 1) return 5;
  if (-x != -10) return 6;
  if (!x != 0) return 7;
  if ((x + y) * 2 - 8 / 2 != 22) return 8;

  if (add(sub(10, 1), add(2, 3)) != 14) return 9;
  if (add(add(1, 2), 3) != 6) return 10;

  printf("run1: %d %d %d %d\n", x, x + y, x * y, add(x, y));
  printf("many args: %d %d %d %d %d %d %d %d\n", 1, 2, 3, 4, 5, 6, 7, 8);

  return 0;
}