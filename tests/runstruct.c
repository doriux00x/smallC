// member access on structs, both '.' and '->'
struct point { int x; int y; };

int dist2(struct point *p) {
  return p->x * p->x + p->y * p->y;
}

// structs by value: parameters and returns are copied with memcpy,
// whole-struct assignment too
struct point mk(int x, int y) {
  struct point r;
  r.x = x;
  r.y = y;
  return r;
}

struct point add(struct point a, struct point b) {
  a.x += b.x;
  a.y += b.y;
  return a;
}

// scalar params after a struct param: the spill order is what the
// memcpy clobbers, so the parked-register machinery matters here
struct point bump(struct point p, int n) {
  p.x += n;
  p.y += n;
  return p;
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

  // struct assign and init from another struct
  struct point b = a;
  if (b.x != 10 || b.y != 8) return 8;
  b = mk(1, 2);
  if (b.x != 1 || b.y != 2) return 9;

  // struct params and returns by value
  struct point c = add(b, mk(5, 6));
  if (c.x != 6 || c.y != 8) return 10;

  // chained assignment evaluates the copies in order
  a = b = c;
  if (a.x != 6 || a.y != 8) return 11;
  if (b.x != 6 || b.y != 8) return 12;

  // member of a returned struct, and a returned struct fed straight
  // into a by-value parameter
  struct point d = mk(mk(1, 2).x, mk(3, 4).y);
  if (d.x != 1 || d.y != 4) return 13;
  if (add(d, mk(2, 2)).y != 6) return 14;

  // copying into a nested member
  wp->p[0] = mk(9, 9);
  if (w.p[0].x != 9 || w.p[0].y != 9) return 15;

  // struct copy through a loop, to exercise the spill machinery
  int k = 0;
  struct point acc = mk(0, 0);
  for (; k < 3; k++)
    acc = add(acc, mk(1, 1));
  if (acc.x != 3 || acc.y != 3) return 16;

  // scalar parameter right after a by-value struct parameter
  struct point e = bump(mk(10, 20), 5);
  if (e.x != 15 || e.y != 25) return 17;

  // flexible array members: the last member may be an incomplete
  // array; sizeof only counts the fixed prefix, and the array hangs
  // off the end of whatever buffer the caller actually provides
  struct fam { int n; char data[]; };
  struct fami { int n; int a[]; };
  struct falign { char c; double d[]; };
  struct fam2d { int n; int a[][3]; };
  struct wrapper { struct fami f; char tag; };
  if (sizeof(struct fam) != 4) return 18;
  if (sizeof(struct fami) != 4) return 19;
  if (sizeof(struct falign) != 8) return 20;
  if (sizeof(struct fam2d) != 4) return 21;
  if (sizeof(struct wrapper) != 8) return 22;
  char fampool[256];
  struct fami *fs = (struct fami *)fampool;
  fs->n = 3;
  fs->a[0] = 10;
  fs->a[1] = 20;
  fs->a[2] = 30;
  if (fs->a[1] != 20) return 23;
  if (fs->n != 3) return 24;
  int *fq = &fs->a[0];
  if (fq[2] != 30) return 25;
  if (((int *)fampool)[1] != 10) return 26;
  struct wrapper *fw = (struct wrapper *)fampool;
  fw->f.a[0] = 7;
  fw->f.a[1] = 8;
  if (fw->f.a[0] + fw->f.a[1] != 15) return 27;
  struct falign *ffa = (struct falign *)fampool;
  ffa->c = 'x';
  ffa->d[0] = 1.5;
  ffa->d[1] = 2.5;
  if (ffa->c != 'x') return 28;
  if (ffa->d[0] + ffa->d[1] != 4.0) return 29;
  struct fam2d *f2 = (struct fam2d *)fampool;
  f2->a[1][2] = 9;
  if (f2->a[1][2] != 9) return 30;

  printf("struct ok\n");
  return 0;
}