// double support: literals, arithmetic, conversions and SysV
// SSE calling conventions, executed end to end
double twice(double x) { return x * 2; }

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

  // nested double expressions must not clobber the operands
  double r = add4(1, 2.0, 3, 4.0);
  if (1.5 + r * 2.0 - 3.5 / 2 != 19.75) return 26;
  r = -r * 2.0;
  if (r != -20.0) return 27;
  if (r + neg2(1.5, 4) != -17.5) return 28;

  printf("float ok\n");
  return 0;
}
