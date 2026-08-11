// double support: literals, arithmetic, conversions, comparisons
// (dump mode: front end + sema only)

int main() {
  double d = 1.5;
  d = d + 2.25;
  d = d * 2.0 / 2;
  d = 1.5 + 2 * 3.5 - 0.25e1;
  d = 1.e1;
  d = .5;
  d = -d;
  d = -1.5 + 2;
  d = !d;
  d = !0.5;
  d = d + 1;
  d = 1 + d;
  d = d - 1.5;
  d = 2.5 - d;
  d = d * 1.5;
  d = 6.0 / 4;
  d = 6 / 2.5;
  d = d < 3.5;
  d = d > 3.5;
  d = d <= 3.5;
  d = d >= 3.5;
  d = d == 3.5;
  d = d != 3.5;
  d = d < 3;
  d = 3.5 < d;
  d = d && 1.5;
  d = 0.0 || d;
  d = 1.5 ? 3.5 : 4.5;
  d = 2 ? 1.5 : 2.5;
  d = d && 2;
  d = 1 && d;
  d += 1.5;
  d -= 1.5;
  d *= 2;
  d /= 4.0;
  d++;
  ++d;
  d--;
  --d;
  int i = d;
  i = 2.7;
  i = d + 1;
  d = i;
  d = i + 1.5;
  double *p = &d;
  d = *p;
  *p = 1.5;
  d = p[0];
  p[0] = 2.5;
  double a[4];
  a[0] = 1.5;
  d = a[0];
  d = sizeof(double);
  return 0;
}
