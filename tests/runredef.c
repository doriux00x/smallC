#define FOO 1
#define FOO 2
#define BAR(x) x
#define BAR(y) y
#define BAZ 3
#define BAZ  3
#undef QUX
#define QUX 4
int main(void) {
  return FOO == 2 && BAR(5) == 5 && BAZ == 3 && QUX == 4 ? 0 : 1;
}
