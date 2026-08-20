/* comment-free: the Makefile diffs smallcc -E against gcc -E -P byte for byte */
#include <poison.h>
int main(void) {
  int ok = 0;
  return ok == 0 ? 0 : 1;
}
