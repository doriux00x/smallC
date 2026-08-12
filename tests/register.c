register int r = 3;

int sum(register int a, register int b) {
  return a + b;
}

int main() {
  register int i = 0;
  register long l = 0;
  for (i = 0; i < 10; i++)
    l = l + i;
  register int res = sum((int)l, r);
  return res == 48 ? 0 : 1;
}