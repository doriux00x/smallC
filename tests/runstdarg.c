/* stdarg: variadic functions with va_list/va_start/va_arg/va_end
 * and va_copy. covers the register save area, overflow arguments
 * past 6 GP / 8 SSE registers, interleaved int/double args, small
 * integer types, and handing a compiler-built va_list to libc's
 * vprintf (the ABI layout must match glibc's exactly). */

int sum_n(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(ap, int);
  va_end(ap);
  return total;
}

int sum_all(int count, ...) {
  va_list ap;
  va_start(ap, count);
  int total = 0;
  for (int i = 0; i < count; i++)
    total += va_arg(ap, int);
  va_end(ap);
  return total;
}

double avg_d(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double total = 0;
  for (int i = 0; i < n; i++)
    total += va_arg(ap, double);
  va_end(ap);
  return total / n;
}

double mix(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double total = 0;
  for (int i = 0; i < n; i++) {
    int a = va_arg(ap, int);
    double d = va_arg(ap, double);
    total += a + d;
  }
  va_end(ap);
  return total;
}

signed char first_char(int n, ...) {
  va_list ap;
  va_start(ap, n);
  signed char c = va_arg(ap, signed char);
  va_end(ap);
  return c;
}

unsigned pick_uint(int n, ...) {
  va_list ap;
  va_start(ap, n);
  unsigned u = va_arg(ap, unsigned);
  va_end(ap);
  return u;
}

int copy_agree(int n, ...) {
  va_list ap, dupe;
  va_start(ap, n);
  va_copy(dupe, ap);
  int a = va_arg(ap, int) + va_arg(ap, int);
  int b = va_arg(dupe, int) + va_arg(dupe, int);
  va_end(ap);
  va_end(dupe);
  return a == b;
}

int myprintf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vprintf(fmt, ap);
  va_end(ap);
  return r;
}

int main(void) {
  if (sum_n(5, 1, 2, 3, 4, 5) != 15)
    return 1;
  if (sum_n(0) != 0)
    return 2;
  if (sum_all(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
    return 3;   /* 4 overflow to the stack */
  if (sum_all(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13) != 91)
    return 4;
  if (avg_d(4, 1.0, 2.0, 3.0, 4.0) != 2.5)
    return 5;   /* all in xmm regs */
  if (mix(2, 1, 1.5, 2, 2.5) != 7.0)
    return 6;   /* int, double, int, double interleaved */
  if (first_char(1, -7) != -7)
    return 7;
  if (pick_uint(1, 0xdeadbeef) != 0xdeadbeef)
    return 8;
  if (!copy_agree(3, 11, 22, 33))
    return 9;

  int r = myprintf("stdarg demo: %d %d\n", 40, 2);
  if (r <= 0)
    return 10;

  printf("runstdarg ok\n");
  return 0;
}