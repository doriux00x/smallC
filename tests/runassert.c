/* _Static_assert: the condition is a constant expression, evaluated
 * at compile time with the given message on failure. passing asserts
 * vanish; a zero condition or a non-constant expression is a
 * compile-time error, so only passing forms appear here */

_Static_assert(sizeof(int) == 4, "int is 4 bytes");
_Static_assert(sizeof(char) == 1, "char is 1 byte");
_Static_assert(2 + 2 == 4, "arithmetic");
_Static_assert(sizeof(int) > sizeof(char), "ordering");

int main(void) {
  _Static_assert(sizeof(long) >= 4, "long is wide");
  _Static_assert(sizeof(int) * 8 == 32, "32-bit ints");
  return 0;
}
