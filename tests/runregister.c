int sum(register int a, register int b) {
  return a + b;
}

int main() {
  register int i = 0;
  register int total = 0;
  for (i = 1; i <= 100; i++)
    total = total + i;
  if (total != 5050)
    return 1;

  int slot = 0;
  register int *p = &slot;
  *p = 7;
  if (slot != 7)
    return 2;

  register long l = 1 << 20;
  if (l != 1048576)
    return 3;

  register float f = 1.5;
  f = f * 2;
  if (f != 3.0)
    return 4;

  register char c = 'x';
  if (c != 120)
    return 5;

  int n = sum(21, 21);
  if (n != 42)
    return 6;

  register _Bool b = 5;
  if (b != 1)
    return 7;

  if (i != 101 || total != 5050)
    return 8;

  return 0;
}