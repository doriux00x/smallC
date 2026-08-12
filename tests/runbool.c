_Bool gb = 7;
_Bool gz = 0;
_Bool garr[4] = {0, 3, -1, 9};

_Bool is_even(int x) {
  return x % 2 == 0;
}

_Bool negate(_Bool b) {
  return !b;
}

typedef _Bool flag;
const _Bool cg = 3;

int main() {
  if (sizeof(_Bool) != 1)
    return 1;
  if (gb != 1 || gz != 0)
    return 2;
  if (garr[0] != 0 || garr[1] != 1 || garr[2] != 1 || garr[3] != 1)
    return 3;

  _Bool b = 5;
  if (b != 1)
    return 4;
  b = 2 * 3;
  if (b != 1)
    return 5;
  b = 0;
  if (b)
    return 6;
  b = -7;
  if (!b)
    return 7;

  if (is_even(4) != 1 || is_even(5) != 0)
    return 8;
  if (negate(0) != 1 || negate(2) != 0)
    return 9;

  if ((_Bool)3.5 != 1 || (_Bool)0.0 != 0)
    return 10;
  int x = (_Bool)5;
  if (x != 1)
    return 11;

  _Bool arr[3] = {9, 0, 2};
  if (arr[0] != 1 || arr[1] != 0 || arr[2] != 1)
    return 12;

  struct S { _Bool flag; _Bool other; } s;
  s.flag = 100;
  s.other = 0;
  if (s.flag != 1 || s.other != 0)
    return 13;

  _Bool *p = &b;
  b = 0;
  *p = 45;
  if (*p != 1)
    return 14;

  _Bool t = 1 ? 300 : 0;
  if (t != 1)
    return 15;

  _Bool c = 2.5;
  if (c != 1)
    return 16;

  _Bool zero = 0.0;
  if (zero != 0)
    return 17;

  flag f = 4;
  if (f + f != 2 || (int)f + (int)cg != 2)
    return 18;

  _Bool g = 1;
  while (g)
    g = 0;
  if (g)
    return 19;

  return 0;
}