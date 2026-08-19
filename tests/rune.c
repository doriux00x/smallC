/* -E: preprocesses to stdin; the text must render builtins as values,
 * splice includes, honor -D, and compile again (round trip) */
#define TRIPLE(x) ((x) * 3)
#define STR(x) #x
int main(void) {
  int a = TRIPLE(7);
  long k = __COUNTER__;
  long m = __COUNTER__;
  int v = FLAG;
  const char *s = STR(hello world);
  return a == 21 && k == 0 && m == 1 && v == 9 && s[0] == 'h' ? 0 : 1;
}
