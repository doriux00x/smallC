/* brace initializers: arrays, structs, nesting, strings, flex arrays */

struct point { int x; int y; };
struct pw { char c; int x; long l; };
struct outer { struct point p; int n; };

int g3[3] = {10, 20, 30};
int gf[] = {1, 2, 3, 4};
struct point gp = {7, 8};
struct point gps[2] = {{1, 2}, {3, 4}};
struct pw gpad = {'a', 5, 6};
char gs[6] = "hello";
char gsf[] = "hi";
char gm[][4] = {"ab", "cd"};
double gd[3] = {1.5, 2.5};
float gfl[2] = {1.5, 2.0f};
char *gsp = "abc";
struct pw gzero = {0};

int main() {
  int a[3] = {1, 2, 3};
  if (a[0] != 1 || a[1] != 2 || a[2] != 3) return 1;

  int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
  if (m[0][1] != 2 || m[1][2] != 6) return 2;

  int flat[2][2] = {1, 2, 3, 4};
  if (flat[0][1] != 2 || flat[1][1] != 4) return 3;

  int flex[] = {5, 6, 7};
  if (flex[0] != 5 || flex[2] != 7) return 4;

  int pa[4] = {1, 2};
  if (pa[2] != 0 || pa[3] != 0) return 5;

  struct point p = {3, 4};
  if (p.x != 3 || p.y != 4) return 6;

  struct point p1 = {9};
  if (p1.x != 9 || p1.y != 0) return 7;

  struct point ps[2] = {{1, 2}, {3, 4}};
  if (ps[0].y != 2 || ps[1].x != 3) return 8;

  struct point pflat[2] = {10, 20, 30, 40};
  if (pflat[0].x != 10 || pflat[1].y != 40) return 9;

  struct pw pad = {'q', 7, 8};
  if (pad.c != 'q' || pad.x != 7 || pad.l != 8) return 10;

  struct outer o = {{1, 2}, 3};
  if (o.p.y != 2 || o.n != 3) return 11;

  struct { int a[2]; int b; } s = {5, 6, 7};
  if (s.a[0] != 5 || s.a[1] != 6 || s.b != 7) return 12;

  char str[6] = "hello";
  if (str[0] != 'h' || str[4] != 'o' || str[5] != 0) return 13;

  char str2[] = "hi";
  if (str2[1] != 'i' || str2[2] != 0) return 14;

  char grid[][4] = {"ab", "cd"};
  if (grid[0][0] != 'a' || grid[0][2] != 0 || grid[1][1] != 'd') return 15;

  double d[3] = {1.5, 2.5};
  if (d[0] != 1.5 || d[2] != 0.0) return 16;

  float fl[2] = {1.5, 2};
  if (fl[0] < 1.49 || fl[0] > 1.51) return 17;
  if (fl[1] < 1.99 || fl[1] > 2.01) return 18;

  int x = {7};
  if (x != 7) return 19;

  int ce[2] = {1 + 2, 3 * 4};
  if (ce[0] != 3 || ce[1] != 12) return 20;

  int z[5] = {};
  if (z[3] != 0) return 21;

  struct point pc = p;
  if (pc.x != 3 || pc.y != 4) return 22;

  if (g3[1] != 20) return 23;
  if (gf[3] != 4) return 24;
  if (gp.x != 7 || gp.y != 8) return 25;
  if (gps[1].x != 3) return 26;
  if (gpad.c != 'a' || gpad.x != 5 || gpad.l != 6) return 27;
  if (gs[4] != 'o' || gs[5] != 0) return 28;
  if (gsf[0] != 'h' || gsf[2] != 0) return 29;
  if (gm[1][0] != 'c' || gm[0][3] != 0) return 30;
  if (gd[0] != 1.5 || gd[2] != 0.0) return 31;
  if (gfl[1] < 1.99 || gfl[1] > 2.01) return 32;
  if (gsp[0] != 'a' || gsp[2] != 'c') return 33;
  if (gzero.c != 0 || gzero.x != 0 || gzero.l != 0) return 34;

  printf("runinit ok\n");
  return 0;
}
