// member access on structs, both '.' and '->'
struct point { int x; int y; };

int dist2(struct point *p) {
  return p->x * p->x + p->y * p->y;
}

int main() {
  struct point a;
  a.x = 3;
  a.y = 4;
  if (dist2(&a) != 25) return 1;

  struct point *p = &a;
  p->x = 6;
  p->y = 8;
  if (dist2(p) != 100) return 2;

  int *px = &a.x;
  *px = 10;
  if (a.x != 10) return 3;
  if (a.y != 8) return 4;

  // nested member access
  struct bw { struct point p[2]; int c; };
  struct bw w;
  w.p[0].x = 1;
  w.p[0].y = 2;
  w.p[1].x = 1;
  w.p[1].y = 2;

  // chained access without intermediate variables
  struct bw *wp = &w;
  wp->p[1].x = 7;
  if (wp->p[1].x != 7) return 5;
  if (wp->p[1].y != 2) return 6;
  if (w.p[0].x != 1) return 7;

  printf("struct ok\n");
  return 0;
}