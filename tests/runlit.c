/* C99 literal suffixes: U, L, LL in any order and case, and the L
 * that makes a floating literal long double (here: double). literals
 * are read as 32-bit quantities, so big values appear through shifts:
 * 1UL << 40. every check adds 1 to the counter. */

int main(void) {
  int check = 0;

  /* the suffix types the literal */
  check += (1U == 1);
  check += (1L == 1);
  check += (1LL == 1);
  check += (1ul == 1UL);          /* case-insensitive */
  check += (1llu == 1ULL);
  check += (1LU == 1UL);          /* either order */
  check += (1LU == 1lu);

  /* long values come from shifts: the L suffix makes the register
   * work 64-bit, so the shift is not truncated to 32 */
  check += ((1UL << 40) > 0);
  check += ((1UL << 40) >> 40 == 1);
  check += (-1L >> 40 == -1);     /* arithmetic shift: signed long */
  check += ((1LL << 63) < 0);     /* the sign bit is set */
  check += ((1UL << 63) > 0);     /* unsigned: no sign bit */
  check += ((1 << 31) > 0);       /* int still wraps at 32 bits */

  /* unsigned arithmetic wraps and divides unsigned */
  check += (4294967295U + 1U == 0U);
  check += (4294967295U / 2U == 2147483647U);
  check += (0xFFFFFFFFU == 4294967295U);
  check += (0x80000000U == 2147483648U);
  check += (1UL != -1);           /* unsigned vs signed compare */
  check += (4294967295U > 0);     /* stored as 0xffffffff, not -1 */

  /* long double literals are doubles here */
  check += (1.5L == 1.5);
  check += (0.5L * 2.0L == 1.0L);

  if (check != 20)
    return check;
  printf("runlit ok\n");
  return 0;
}
