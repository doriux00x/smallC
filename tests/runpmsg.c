/* comment-free is not required here: the Makefile compares stderr
 * notes and exit codes, not the -E text (gcc keeps pragma lines in
 * its own -E output, ours strips them like every other pragma) */
#define BANNER "stage one"
#pragma message(BANNER)
#pragma message("a" "b")
#pragma message("plain")
int main(void) {
  return 0;
}
