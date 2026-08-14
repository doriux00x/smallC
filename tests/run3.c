int fib(int n) {
  if (n == 0)
    return 0;
  if (n == 1)
    return 1;
  return fib(n - 1) + fib(n - 2);
}

int main() {
  if (fib(10) != 55) return 1;

  char c = 'a';
  c++;
  if (c != 'b') return 2;

  char *p = "hello";
  int len = 0;
  while (p[len])
    len++;
  if (len != 5) return 3;

  if (sizeof("hello") != 6) return 4;

  unsigned char u = 200;
  if (u + 100 != 300) return 5;
  if (u < 150) return 6;

  /* escape sequences: \a \b \f \v \?, octal, and hex */
  if ('\a' != 7) return 7;
  if ('\b' != 8) return 8;
  if ('\f' != 12) return 9;
  if ('\v' != 11) return 10;
  if ('\?' != '?') return 11;
  if ('\101' != 'A') return 12;
  if ('\0' != 0) return 13;
  if ('\141' != 'a') return 14;
  if ('\x41' != 'A') return 15;
  if ('\x2a' != '*') return 16;
  if ('\xff' != -1) return 17;   /* execution char is signed: -1, as gcc */
  if ('\xFF' != '\xff') return 18;
  if ('\7' != '\a') return 19;
  if ('\17' != 15) return 20;
  if (strcmp("\x41\x42\101", "ABA") != 0) return 21;
  if (strcmp("\a\b\f\v\?\x41", "\7\10\14\13?A") != 0) return 22;
  char *esc = "\x68\x65\x6c\x6c\x6f";
  if (strcmp(esc, "hello") != 0) return 23;
  if (sizeof("a\x00b") != 3) return 24;   /* \x00b is one hex escape */
  if ('\x00b' != 11) return 25;

  printf("run3: fib=%d\n", fib(10));
  return 0;
}