int main() {
  int i = 0;
  int sum = 0;
start:
  i = i + 1;
  sum = sum + i;
  if (i < 10)
    goto start;
  if (sum != 55)
    return 1;

  goto inner;
  if (i != 0)
    return 2;
inner:
  if (i != 10)
    return 3;

  int n = 0;
  while (i < 20) {
    i++;
    if (i % 2)
      goto cont;
    n++;
  cont:
    continue;
  }
  if (n != 5)
    return 4;

  int j = 0;
  for (;;) {
    j++;
    if (j == 4)
      goto out;
  }
out:
  if (j != 4)
    return 5;

  int k = 0;
  {
    goto inblock;
  inblock:
    k = 7;
  }
  if (k != 7)
    return 6;

  switch (2) {
  case 1:
    return 7;
  case 2:
    goto sw;
  case 3:
    return 8;
  }
sw:
  return 0;
}