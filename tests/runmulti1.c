int gv = 21;
static int hidden = 99;

int mul2(int x) {
  return x * 2;
}

int bump(int delta) {
  gv = gv + delta;
  return gv;
}

int unseen(void) {
  return hidden;
}