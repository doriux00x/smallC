/* comment-free: the Makefile diffs smallcc -E against gcc -E -P byte for byte */
#include <chain.h>
#include <chain2.h>
int main(void) {
  return LEAF == 2 ? 0 : 1;
}
