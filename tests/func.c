/* function definitions, params, and the statement grammar */

int add(int a, int b) {
  return a + b;
}

int fib(int n) {
  if (n < 2)
    return n;
  return fib(n - 1) + fib(n - 2);
}

int abs(int x) {
  if (x < 0)
    return -x;
  else
    return x;
}

/* else binds to the nearest if */
int both(int a, int b) {
  if (a)
    if (b)
      return 10;
    else
      return 20;
  return 30;
}

int countdown(int n) {
  while (n > 0) {
    n = n - 1;
  }
  return n;
}

int sum_up_to(int n) {
  int sum = 0;
  int i;
  for (i = 1; i <= n; i = i + 1)
    sum = sum + i;
  return sum;
}

int sum_range(int a, int b) {
  int s = 0;
  for (int i = a; i <= b; i++)
    s += i;
  return s;
}

int first_even(int limit) {
  int i = 0;
  do {
    i = i + 1;
  } while (i % 2 != 0 && i < limit);
  return i;
}

int scan(int needle, int n) {
  int i = 0;
  int seen = 0;
  while (i < n) {
    if (needle == i) {
      seen = 1;
      break;
    }
    i++;
  }
  return seen;
}

int skip_three(int n) {
  int i = 0, total = 0;
  while (i < n) {
    i = i + 1;
    if (i == 3)
      continue;
    total = total + i;
  }
  return total;
}

int forever(void);

void putchar(char c);

int smallest(int a, int *b);

int one(void) {
  return 1;
}

int empty(void) {
}

void *grow(int *buf, char *name);

char *greet(void) {
  return "hi";
}