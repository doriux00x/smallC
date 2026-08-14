// variable-length arrays: int a[n], with runtime sizeof and strides
int printf(const char *fmt, ...);

int sum(int n, int a[n]) {
  int t = 0;
  for (int i = 0; i < n; i++)
    t += a[i];
  return t;
}

/* a VLA parameter decays to a pointer, so the [n] costs nothing */
int first(int n, int a[n]) { return a[0] + n; }

int main() {
  int n = 5;
  int vla[n];
  for (int i = 0; i < n; i++)
    vla[i] = i * 10;
  if (vla[3] != 30) return 1;
  if (sizeof(vla) != 5 * 4) return 2;
  if (sum(n, vla) != 100) return 3;
  if (first(n, vla) != 5) return 4;

  /* mixed dims: constant outer, VLA inner */
  int m = 3;
  int w[2][m];
  w[1][2] = 7;
  if (w[1][2] != 7) return 5;
  if (sizeof(w) != 2 * 3 * 4) return 6;
  if (sizeof(w[1]) != 3 * 4) return 7;

  /* sizeof of a VLA type */
  if (sizeof(int[n]) != 20) return 8;
  if (sizeof(int[2][n]) != 40) return 9;

  /* a computed size, and a size that changes between uses */
  int k = n - 2;
  char str[k + 1];
  if (sizeof(str) != 4) return 10;
  str[0] = 'x';
  str[1] = 'y';
  str[2] = 'z';
  str[3] = 0;
  if (str[2] != 'z') return 11;

  /* both dims dynamic: indexing steps by a runtime stride */
  int p = 2, q = 4;
  int w2[p][q];
  for (int i = 0; i < p; i++)
    for (int j = 0; j < q; j++)
      w2[i][j] = i * 100 + j;
  if (w2[1][3] != 103) return 12;
  if (sizeof(w2) != p * q * 4) return 13;
  if (sizeof(w2[1]) != q * 4) return 14;

  /* a VLA in a loop re-allocates every iteration */
  int total = 0;
  for (int i = 1; i <= 4; i++) {
    int row[i];
    for (int j = 0; j < i; j++)
      row[j] = j;
    total += row[i - 1];
  }
  if (total != 6) return 15;

  /* the size expression is evaluated where the array is declared,
   * so the value it sees is the one at that point */
  int z = 2;
  int zm[z];
  z = 9;
  if (sizeof(zm) != 8) return 16;

  printf("runvla ok\n");
  return 0;
}
