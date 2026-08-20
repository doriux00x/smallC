/* comment-free: the Makefile diffs smallcc -E against gcc -E -P byte for byte */
int main(void) {
  return CHEESE == 1 ? 0 : 1;
}
