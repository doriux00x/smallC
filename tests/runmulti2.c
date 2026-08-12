extern int gv;
extern int mul2(int);
extern int bump(int);
extern int unseen(void);

int main() {
  if (mul2(gv) != 42)
    return 1;
  if (bump(1) != 22)
    return 2;
  if (gv != 22)
    return 3;
  if (unseen() != 99)
    return 4;
  return 0;
}