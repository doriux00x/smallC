// struct declaration dump test; run with -a and eyeball the layout
struct point { int x; int y; } origin;

// structs can reference themselves through pointers
struct node { int val; struct node *next; };

struct point *mk(int x, int y);

int sum(struct node *head) {
  int s = 0;
  for (; head; head = head->next)
    s += head->val;
  return s;
}

struct wrapper {
  char c;
  struct point p;      // offset must be 4 after padding
  long l;              // offset must be 16
};

// anonymous struct types in the middle of declarators
int f(void) {
  struct { int a; int b; } anon;
  anon.a = 1;
  return anon.b + anon.a;
}

// struct + array mixing: alignment of the element type
// (keep it integer-only: no float backend yet)
struct cell { char tag; long size; };
struct cell grid[4][4];

// structs by value: params, returns (via the hidden ~ret buffer) and
// whole-struct assignment are all memcpy's in the backend
struct point byvalue(struct point p, int n) {
  p.x += n;
  p.y += n;
  return p;
}

int byvalue2(struct point p) {
  return p.x * 10 + p.y;
}

int main() {
  struct point a;
  a.x = 1;
  a.y = 2;
  struct node n1, n2;
  n1.val = 3;
  n1.next = &n2;
  n2.val = 4;
  n2.next = 0;
  if (sum(&n1) != 7) return 1;
  struct wrapper w;
  w.c = 'x';
  w.p.x = 5;
  w.p.y = 6;
  w.l = 7;
  grid[2][3].tag = 'g';
  grid[2][3].size = 4;
  if (grid[2][3].size != 4) return 2;
  struct point b = a;
  if (b.x != 1 || b.y != 2) return 3;
  b = byvalue(a, 5);
  if (b.x != 6 || b.y != 7) return 4;
  if (byvalue2(b) != 67) return 5;
  return 0;
}