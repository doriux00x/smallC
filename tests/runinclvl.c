/* comment-free: the Makefile diffs smallcc -E against gcc -E -P byte for byte */
#include <depth1.h>
const int d0 = __INCLUDE_LEVEL__;
const char *f0 = __FILE_NAME__;
int main(void) {
  return D1 == 1 && D2 == 2 && d0 == 0 && f0[0] != '\0' ? 0 : 1;
}
