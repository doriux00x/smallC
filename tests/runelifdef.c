/* #elifdef/#elifndef (C23): this file is comment-free so the
 * Makefile can diff smallcc's -E against gcc -E -P byte for byte */
#define LEVEL 2
int main(void) {
  int v = 0;
#if LEVEL == 1
  v = 1;
#elifdef ENABLED
  v = 2;
#elif LEVEL == 2
  v = 3;
#else
  v = 4;
#endif
#if 0
  v = 5;
#elifndef MISSING
  v = 6;
#elifdef LEVEL
  v = 7;
#else
  v = 8;
#endif
  return v == 6 ? 0 : 1;
}
