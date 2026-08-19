/* the Makefile diffs smallcc's stderr against gcc -fsyntax-only's
 * on this file, so both compilers must format the warning alike */
#define LEVEL 2
int main(void) {
#if LEVEL == 2
  #warning "level two build"
#else
  #warning "other level"
#endif
#if 0
  #warning "dead branch never warns"
#endif
  return 0;
}
