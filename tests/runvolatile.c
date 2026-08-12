volatile int gv = 5;
const volatile int gcv = 9;
volatile int garr[3] = {1, 0, 2};

int main() {
  volatile int v = 0;
  v = 7;
  if (v != 7)
    return 1;
  v = v + 1;
  if (v != 8)
    return 2;

  int volatile tv = 3;
  if (tv != 3)
    return 3;

  volatile long vl = 5;
  vl = vl * 2;
  if (vl != 10)
    return 4;

  volatile int *p = &v;
  *p = 11;
  if (v != 11)
    return 5;

  int flag = 12;
  int *volatile q = &flag;
  *q = 13;
  if (q != &flag || flag != 13)
    return 6;

  volatile const int c = 100;   /* qualified both ways */
  if (c != 100)
    return 7;

  volatile int a = gv + gcv;
  if (a != 14)
    return 8;

  if (garr[0] != 1 || garr[1] != 0 || garr[2] != 2)
    return 9;

  struct S { volatile int flag; int rest; } s;
  s.flag = 42;
  s.rest = 1;
  if (s.flag != 42 || s.rest != 1)
    return 10;

  return 0;
}