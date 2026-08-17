/* 64-bit integer literals: the value survives the lexer whole and
 * the type follows gcc's ladder (decimal: int, long, unsigned long;
 * hex/octal: int, unsigned, long, unsigned long; the suffix narrows
 * the ladder to its own class). every check adds 1 to the counter;
 * the fail branches add 1000 so any mis-evaluated comparison blows
 * the total. cross-checked with gcc on the same file. */

int printf(const char *, ...);

/* global initializers fold through const_fold, a 64-bit walk too */
unsigned long g_ulong = 0x1234567890ABCDEFUL;
long g_long = 0x7FFFFFFFFFFFFFFF;
long g_neg = -0x8000000000000000;

/* big constants in enum values stay int-sized when they fit:
 * 0xFFFFFFFF is an unsigned int, so an enum can hold it as -1 */
enum { E_BIG = 0xFFFFFFFF };

int main(void) {
  int check = 0;

  /* the type ladder, probed through sizeof */
  check += (sizeof(2147483647) == 4);
  check += (sizeof(2147483648) == 8);              /* decimal: long */
  check += (sizeof(0xFFFFFFFF) == 4);              /* hex: unsigned int */
  check += (sizeof(0x100000000) == 8);             /* hex: long */
  check += (sizeof(1U) == 4);                      /* U: unsigned int */
  check += (sizeof(4294967296U) == 8);             /* U past 32 bits: unsigned long */
  check += (sizeof(9223372036854775807) == 8);     /* max long */
  check += (sizeof(0xFFFFFFFFFFFFFFFF) == 8);      /* past LONG_MAX: unsigned */
  check += (sizeof(0xFFFFFFFFFFFFFFFFUL) == 8);

  /* the values ride the whole way through */
  check += (0xFFFFFFFFFFFFFFFFUL == 18446744073709551615UL);
  check += (0x100000000 == 4294967296);
  check += (18446744073709551615UL / 2 == 9223372036854775807UL);
  check += (0x1000000000000000ULL / 0x100000000 == 0x10000000);
  check += (18446744073709551615UL % 7 == 1);
  check += (0xFFFFFFFFFFFFFFFFUL >> 63 == 1);      /* unsigned shr, not sar */
  check += ((18446744073709551615UL - 1) * 2 == 18446744073709551612UL);
  check += (18446744073709551614UL < 18446744073709551615UL);
  check += (0xFFFFFFFF == 4294967295);             /* uint == long */
  check += (4294967295 > 0);                       /* no longer -1 */
  check += (0x100000000 > 2147483647);             /* long vs int */
  check += (-0x8000000000000000 == -9223372036854775807L - 1);
  check += (0x8000000000000000UL - 1 == 9223372036854775807UL);
  check += (0xFFFFFFFFU >> 31 == 1);               /* uint shift, shr */
  check += (0x80000000U == 2147483648U);

  /* the 40-bit bit-field arithmetic from runbit, now honest: the
   * literal is -0x8000000000 + 3 on both sides of the compare */
  struct { long a : 40; } lg;
  lg.a = -0x8000000000 + 3;
  check += (lg.a == -0x8000000000 + 3);

  /* globals: folded at compile time into .quad */
  check += (g_ulong == 0x1234567890ABCDEFUL);
  check += (g_long == 0x7FFFFFFFFFFFFFFF);
  check += (g_neg == -0x8000000000000000);
  check += (g_ulong >> 60 == 1);

  /* the enum held its bits */
  check += (E_BIG == (int)0xFFFFFFFF);

  if (check != 30)
    return check;
  printf("runlong ok\n");
  return 0;
}