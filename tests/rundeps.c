/* comment-free not needed: the Makefile diffs the -M rule against
 * gcc's (with -nostdinc so neither side lists system headers) */
#include <poison.h>
#include <chain.h>
#include "depth1.h"
#include "depth1.h"
#define HDR "inc/cheese.h"
#include HDR
int main(void) {
  return 0;
}
