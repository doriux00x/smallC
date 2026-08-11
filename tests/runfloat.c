// double/float support: literals, arithmetic, conversions, casts and
// SysV SSE calling conventions, executed end to end
double twice(double x) { return x * 2; }

float half2(float x) { return x / 2; }

float mixf(float a, int b, double c) { return a + b + c; }

// global initializers, folded at compile time
double gd = 1.5;
float gf = 1.5f;
double ge = 1.5 + 2;
double gmix = 1.5 * 2 - 0.5;
double gneg = -2.5;
double g1 = 1;
int gi = 2.7;
float gcast = (float)2.75;
double gfold = 1.5 ? 2.5 : 3.5;
double gcmp = 1.5 < 2.5;
double gnot = !0.0;
double gd2 = (double)3;
double gbss;
float gbssf;

// mixed int/double arguments, all four register classes
double add4(int a, double b, int c, double d) {
  return a + b + c + d;
}

// many double arguments spill onto the stack
double sum5(double a, double b, double c, double d, double e) {
  return a + b + c + d + e;
}

double neg2(double x, int y) { return -x + y; }

int main() {
  if (twice(2.5) != 5.0) return 1;
  if (add4(1, 2.5, 3, 0.5) != 7.0) return 2;
  if (sum5(1.0, 2.0, 3.0, 4.0, 5.0) != 15.0) return 3;
  if (neg2(1.5, 4) != 2.5) return 4;

  // printf is varargs: the %al argument count has to be right
  printf("%f\n", twice(2.5));
  printf("%f %.2f\n", 1.5 + 2, twice(3.0));

  double d = 1;
  d += 0.5;
  if (d != 1.5) return 5;
  d++;
  if (d != 2.5) return 6;
  if (!d) return 7;
  if (!(d && 2.5)) return 8;
  if (!(0.0 || d)) return 9;

  // conversions in both directions
  int i = 2.7;
  if (i != 2) return 10;
  double e = i;
  if (e != 2.0) return 11;
  i = 1 + 2.5;
  if (i != 3) return 14;
  if (!(0.5 < 1)) return 15;
  if (!(2.5 > 2)) return 16;
  int x = 1.5 ? 3 : 4;
  if (x != 3) return 17;
  x = 0.0 ? 3 : 4;
  if (x != 4) return 18;

  // pointers and arrays
  double *p = &d;
  if (*p != 2.5) return 19;
  *p = 5.0;
  if (d != 5.0) return 20;
  double a[3];
  a[0] = 1.5;
  a[1] = 2.5;
  a[2] = a[0] + a[1];
  if (a[2] != 4.0) return 21;
  if (sizeof(double) != 8) return 22;

  // loops with double conditions and counters
  int n = 0;
  double acc = 0;
  while (acc < 1.5) { acc += 0.5; n++; }
  if (n != 3) return 23;
  do { acc -= 1.0; } while (acc > 0.5);
  if (acc != 0.5) return 24;
  for (double j = 0; j < 2.0; j++)
    acc += 1.0;
  if (acc != 2.5) return 25;
  double dec = 4.5;
  dec--;
  if (dec != 3.5) return 60;
  double pre = dec--;
  if (pre != 3.5) return 61;
  if (dec != 2.5) return 62;

  // nested double expressions must not clobber the operands
  double r = add4(1, 2.0, 3, 4.0);
  if (1.5 + r * 2.0 - 3.5 / 2 != 19.75) return 26;
  r = -r * 2.0;
  if (r != -20.0) return 27;
  if (r + neg2(1.5, 4) != -17.5) return 28;

  // casts
  if ((int)2.9 != 2) return 29;
  if ((int)2.5f != 2) return 30;
  if ((double)3 != 3.0) return 31;
  if ((float)2.75 != 2.75f) return 32;
  if ((float)1 != 1.0f) return 33;
  if ((double)(int)3.5 != 3.0) return 34;

  // float: literals, arithmetic, conversions
  float f = 1.5f;
  if (f != 1.5f) return 35;
  f = f + 0.25f;
  if (f != 1.75f) return 36;
  f = f * 2.0f;
  if (f != 3.5f) return 37;
  f = half2(3.0f);
  if (f != 1.5f) return 38;
  f = 1.5f + 2;
  if (f != 3.5f) return 39;
  double dd = f;
  if (dd != 3.5) return 40;
  f = (float)dd;
  if (f != 3.5f) return 41;
  int ii = (int)f;
  if (ii != 3) return 42;
  f = (float)ii;
  if (f != 3.0f) return 43;
  float g = 1;
  if (g != 1.0f) return 44;
  if (!g) return 45;
  if (!(g && 1.0f)) return 46;
  if (!(0.0f || g)) return 47;
  g += 2.5f;
  if (g != 3.5f) return 48;
  g++;
  if (g != 4.5f) return 49;
  g--;
  if (g != 3.5f) return 50;
  if (!(f < g)) return 51;
  float h = -g;
  if (h != -3.5f) return 52;
  float arr[2];
  arr[0] = 1.5f;
  arr[1] = 2.5f;
  if (arr[0] + arr[1] != 4.0f) return 53;
  float *pf = &arr[1];
  if (*pf != 2.5f) return 54;
  int t = 1.5f ? 3 : 4;
  if (t != 3) return 55;
  if (mixf(1.5f, 2, 3.5) != 7.0f) return 56;
  if (sizeof(float) != 4) return 57;
  f = 1f;
  if (f != 1.0f) return 58;
  while (g > 0.5f) { g -= 1.0f; }
  if (g != 0.5f) return 59;

  // global initializers
  if (gd != 1.5) return 63;
  if (gf != 1.5f) return 64;
  if (ge != 3.5) return 65;
  if (gmix != 2.5) return 66;
  if (gneg != -2.5) return 67;
  if (g1 != 1.0) return 68;
  if (gi != 2) return 69;
  if (gcast != 2.75f) return 70;
  if (gfold != 2.5) return 71;
  if (gcmp != 1.0) return 72;
  if (gnot != 1.0) return 73;
  if (gd2 != 3.0) return 74;
  if (gbss != 0.0) return 75;
  if (gbssf != 0.0f) return 76;
  gd = 9.25;
  gf = 2.5f;
  gd += gbss;
  if (gd != 9.25) return 77;
  gf *= 2;
  if (gf != 5.0f) return 78;
  double *gp = &gd;
  *gp = 1.25;
  if (gd != 1.25) return 79;
  printf("%f %f\n", gd, gf + 1.5f);

  printf("float ok\n");
  return 0;
}
