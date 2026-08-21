/* grep-checked: each stamp comes from its own file's mtime, and the
 * -E text is diffed against gcc's byte for byte */
#include <tstamp.h>
const char *main_ts = __TIMESTAMP__;
int main(void) {
  return main_ts[0] && hdr_ts[0] ? 0 : 1;
}
