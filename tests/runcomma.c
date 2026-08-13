/* the comma operator: lowest precedence, strict left to right with
 * a sequence point, the value of the expression is the right
 * operand. every failing check returns its own number. */

int counter;

int bump(int d) {
  counter += d;
  return d;
}

int rettwo(void) {
  return bump(7), 2;
}

int main(void) {
  int i, j;

  /* two counters in the for clauses */
  int sum = 0;
  for (i = 0, j = 10; i < 5; i++, j--)
    sum += j;
  if (sum != 40)
    return 1;
  if (i != 5 || j != 5)
    return 2;

  /* the value of a comma expression is the right operand */
  if ((1, 2) != 2)
    return 3;

  /* everything on the left still evaluates, left to right */
  if ((bump(5), bump(3), bump(1)) != 1)
    return 4;
  if (counter != 9)
    return 5;

  /* a sequence point: a write on the left is visible on the right */
  int k = 0;
  if ((k = 6, k) != 6)
    return 6;

  /* comma binds looser than assignment */
  i = 1, j = 2;
  if (i != 1 || j != 2)
    return 7;

  /* comma in a return: the returned value is the right operand */
  if (rettwo() != 2)
    return 8;
  if (counter != 16)   /* the bump(7) in the return still ran */
    return 9;

  /* enum values, bit-field widths, case labels and designator
   * indices stay comma-free: the comma there is a separator */
  enum { EZ = 5, EF = 8 };
  struct S { int hi : 2, lo : 4; };
  struct S s;
  s.hi = 1, s.lo = 7;
  if (EZ + EF != 13)
    return 10;
  if (s.hi != 1 || s.lo != 7)
    return 11;

  printf("runcomma ok\n");
  return 0;
}