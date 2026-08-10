int main() {
  int sum = 0;
  for (int i = 1; i <= 10; i++)
    sum += i;
  if (sum != 55) return 1;

  int j = 0;
  while (j < 5) {
    j++;
    if (j == 3)
      continue;
    sum += j;
  }
  if (sum != 67) return 2;

  int k = 0;
  do {
    k += 2;
    if (k > 8)
      break;
  } while (k < 20);
  if (k != 10) return 3;

  int n = 0;
  for (int a = 0; a < 3; a++)
    for (int b = 0; b < 3; b++)
      n++;
  if (n != 9) return 4;

  printf("run2 ok\n");
  return 0;
}