/* no comments: the Makefile diff of smallcc -E against gcc -E -P is byte for byte */
#define HDR "inc/cheese.h"
int main(void) {
  int a;
#include HDR
  a = CHEESE;
  return a == CHEESE ? 0 : 1;
}
