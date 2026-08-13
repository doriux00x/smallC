/* C99 compound literals: an anonymous initialized object usable as
 * an lvalue in any expression, block scope only. covers scalar,
 * struct, array and char-array literals, member/index access on the
 * literal itself, address-of, argument passing, and assignment. */

struct point { int x, y; };

int sum_p(struct point p) { return p.x + p.y; }

int main(void) {
  if (sum_p((struct point){3, 4}) != 7)
    return 1;
  if (sum_p((struct point){-1, 1}) != 0)
    return 2;

  int *a = (int[]){10, 20, 30};
  if (a[0] + a[1] + a[2] != 60)
    return 3;
  if ((int[]){1, 2, 3, 4}[3] != 4)
    return 4;

  struct point q = (struct point){5, 6};
  if (q.x != 5 || q.y != 6)
    return 5;
  q = (struct point){7, 8};
  if (q.x != 7 || q.y != 8)
    return 6;

  if ((int){42} != 42)
    return 7;
  if ((struct point){9, 10}.y != 10)
    return 8;

  int *px = &(int){77};
  if (*px != 77)
    return 9;

  if ((double){2.5} + (double){1.5} != 4.0)
    return 10;
  if ((char[4]){'a', 'b', 'c', 'd'}[2] != 'c')
    return 11;

  /* a fresh object every evaluation, like C requires */
  int *z1 = &(int){1};
  int *z2 = &(int){2};
  *z1 = 5;
  if (*z1 != 5 || *z2 != 2)
    return 12;

  printf("runclit ok\n");
  return 0;
}