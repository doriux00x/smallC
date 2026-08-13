/* designated initializers: .member = v, [idx] = v, chains, flex arrays */

struct point { int x; int y; };
struct pw { char c; int x; long l; };
struct np { struct point p; int n; };

int ga[5] = {[3] = 7};
struct point gp = {.y = 4, .x = 3};
int gflex[] = {[2] = 1};

int main() {
  struct point p = {.y = 4, .x = 3};
  if (p.x != 3 || p.y != 4) return 1;

  struct point p1 = {.y = 9};
  if (p1.x != 0 || p1.y != 9) return 2;

  struct point p2 = {.x = 1, 2};
  if (p2.x != 1 || p2.y != 2) return 3;

  struct pw pw1 = {.l = 42};
  if (pw1.c != 0 || pw1.x != 0 || pw1.l != 42) return 4;

  int a[5] = {[2] = 7};
  if (a[0] != 0 || a[2] != 7 || a[4] != 0) return 5;

  int a1[5] = {[1] = 2, 3, 4};
  if (a1[0] != 0 || a1[1] != 2 || a1[2] != 3 || a1[3] != 4 || a1[4] != 0)
    return 6;

  int a2[5] = {1, [3] = 5};
  if (a2[0] != 1 || a2[1] != 0 || a2[2] != 0 || a2[3] != 5) return 7;

  int a3[3] = {1, [0] = 9, 2};
  if (a3[0] != 9 || a3[1] != 2 || a3[2] != 0) return 8;

  int a4[5] = {[1 + 1] = 6};
  if (a4[2] != 6 || a4[3] != 0) return 9;

  int f[] = {[3] = 9};
  if (f[3] != 9 || f[0] != 0) return 10;

  int f1[] = {[1] = 2, 3};
  if (f1[1] != 2 || f1[2] != 3) return 11;

  struct np o = {.p.y = 5, .n = 1};
  if (o.p.x != 0 || o.p.y != 5 || o.n != 1) return 12;

  struct np o1 = {.p = {1, 2}};
  if (o1.p.x != 1 || o1.p.y != 2 || o1.n != 0) return 13;

  struct { int a[2]; int b; } s = {.b = 5, .a = {10, 20}};
  if (s.a[0] != 10 || s.a[1] != 20 || s.b != 5) return 14;

  struct point ps[3] = {[1] = {5, 6}};
  if (ps[0].x != 0 || ps[1].x != 5 || ps[2].y != 0) return 15;

  struct point ps1[3] = {[2].y = 8};
  if (ps1[2].x != 0 || ps1[2].y != 8) return 16;

  union { int i; char c; } u = {.c = 'z'};
  if (u.c != 'z') return 17;

  int (*cl)[4] = &(int[4]){1, [2] = 3};
  if ((*cl)[0] != 1 || (*cl)[2] != 3 || (*cl)[3] != 0) return 18;

  int lf[] = {[5] = 1, 2};
  if (lf[5] != 1 || lf[6] != 2) return 19;

  if (ga[3] != 7 || ga[0] != 0) return 20;
  if (gp.x != 3 || gp.y != 4) return 21;
  if (gflex[2] != 1 || gflex[3] != 0) return 22;

  printf("rundesig ok\n");
  return 0;
}