/* plain -E: gcc's default linemarker form, diffed byte for byte
 * against gcc -E in the runlm stanza. exercises the opening triple,
 * enter/return markers through guarded headers, a pragma-once
 * header included twice, a macro-expanded include name, handled
 * pragmas collapsing to the seven-space row, an unknown pragma
 * passing through verbatim, blank runs at the resync boundary,
 * a skipped region wide enough to force the bare resync marker,
 * and a multi-line #define */
#define HDR "lminc1.h"
#include HDR
#include "lminc2.h"
#include "lminc3.h"

#pragma GCC poison sneaky
#pragma FOO bar baz

#define SPAN \
  1 + \
  2

#if 0
one
two
three
four
five
six
seven
eight
nine
ten
eleven
twelve
#endif

int a[] = {SPAN,

           SPAN};


int main(void) {
  return 0;
}
