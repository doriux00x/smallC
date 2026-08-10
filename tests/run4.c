int main() {
  int a[5];
  for (int i = 0; i < 5; i++)
    a[i] = i * 10;

  int sum = 0;
  for (int i = 0; i < 5; i++)
    sum += a[i];
  if (sum != 100) return 1;

  int *p = &a[0];
  p++;
  if (*p != 10) return 2;
  if (p[1] != 20) return 3;
  if (*(p + 2) != 30) return 4;

  p[2] = 99;
  if (a[3] != 99) return 5;

  char *s = "abc";
  if (s[1] != 'b') return 6;
  s += 1;
  if (*s != 'b') return 7;

  long d = &a[4] - &a[0];
  if (d != 4) return 8;

  printf("run4 ok\n");
  return 0;
}