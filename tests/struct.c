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
struct cell { char tag; double d; };
struct cell grid[4][4];

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
  // grid[2][3].d = 1.0;  // no float literals until the float backend lands
  return 0;
}