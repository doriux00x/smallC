_Bool gb = 7;
_Bool gz = 0;
_Bool garr[4] = {0, 3, -1, 9};

_Bool is_even(int x) {
  return x % 2 == 0;
}

int main() {
  _Bool b = 5;
  b = 0;
  _Bool arr[2] = {9, 0};
  return b + arr[0] + is_even(4);
}