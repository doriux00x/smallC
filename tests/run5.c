int g = 10;
int g2;
char gc = 'z';
long gl = 1000;

int main() {
  g += 5;
  if (g != 15) return 1;
  g2 = 42;
  if (g2 != 42) return 2;
  if (gc != 'z') return 3;
  if (gl != 1000) return 4;

  if ((g > 10 ? g : 0) != 15) return 5;
  if (!(1 && 2)) return 6;
  if (1 || 0) { } else return 7;
  if (!(0 || 0)) { } else return 8;

  if ((2 << 3) != 16) return 9;
  if ((16 >> 2) != 4) return 10;

  unsigned int u = -1;
  if (u / 2 != 2147483647) return 11;
  if (!(u > 10)) return 12;

  if (sizeof(int) != 4) return 13;
  if (sizeof(char *) != 8) return 14;
  if (sizeof(g) != 4) return 15;
  if (sizeof("hi") != 3) return 16;

  if (-7 / 2 != -3) return 17;
  if (-7 % 2 != -1) return 18;

  printf("run5: u/2=%u\n", u / 2);
  return 0;
}