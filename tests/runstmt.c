/* GNU statement expressions: ({ stmt; ...; value; }) turns a block of
 * statements into an expression whose value is the last one */

int helper(int a) { return a + 1; }

int main(void) {
  /* the basic form: statements run, the last expression is the value */
  int side = 0;
  int r = ({ side = 5; side += 2; side * 10; });
  if (r != 70) return 1;
  if (side != 7) return 2;

  /* the value keeps its type, float included */
  double d = ({ double p = 1.5; p + 0.5; });
  if (d != 2.0) return 3;
  float f = ({ float pf = 3.0; pf + 0.5; });
  if (f != 3.5) return 4;
  char c = ({ char pc = 7; pc + 1; });
  if (c != 8) return 5;
  int *p = ({ int val = 3; &val; });
  if (*p != 3) return 6;

  /* locals are scoped to the block and may shadow outer ones */
  int n = 1;
  int g = ({ int n = 100; n + 1; });
  if (g != 101) return 7;
  if (n != 1) return 8;

  /* nested statement expressions */
  if (({ int a = ({ 2; }); a + 3; }) != 5) return 9;

  /* usable anywhere an expression is: a condition, an argument */
  if (({ 0; })) return 10;
  if (!({ 1; })) return 11;
  if (({ int v = 3; v > 2; }) != 1) return 12;
  int sum = 0;
  for (int k = 0; k < 3; k++)
    sum += ({ int z = k * k; z; });
  if (sum != 5) return 13;

  /* a whole function body can be a statement expression */
  if (({ int x = 2; x * 9; }) != 18) return 14;

  /* a void one (ends on a declaration) is fine as a statement */
  ({ int discard = 9; });
  int check = ({ 1; 2; });
  if (check != 2) return 15;

  /* the block can drive flow; intermediate statements run in order */
  int acc = 0;
  int w = ({ acc = 1; while (acc < 3) acc++; acc; });
  if (w != 3) return 16;
  if (acc != 3) return 17;

  /* functions can be called from inside the block */
  if (({ helper(4); }) != 5) return 18;

  printf("runstmt ok\n");
  return 0;
}