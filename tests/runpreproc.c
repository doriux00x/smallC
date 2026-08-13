/* preprocessor: object-like and function-like macros, #if/#ifdef/
 * #ifndef/#elif/#else/#endif with constant expressions and defined(),
 * #undef, quote-form #include resolved against the including file's
 * directory, and the dynamic macros __LINE__/__FILE__/__COUNTER__/
 * __STDC__. every check adds 1 to the counter; the fail branches add
 * 1000 so any mis-evaluated conditional blows the total. */

#include "inc/preproc.h"

#define VERSION 3
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define IS_EVEN(n) ((n) % 2 == 0)
#define GUARD
#define FEATURE 2

#if FEATURE == 1
#define CHOSEN 10
#elif FEATURE == 2
#define CHOSEN 20
#else
#define CHOSEN 30
#endif

int main(void) {
  int check = 0;

  /* object-like and function-like macros */
  check += (VERSION == 3);
  check += (SQUARE(5) == 25);
  check += (SQUARE(VERSION + 1) == 16);
  check += (MAX(4, 9) == 9);
  check += (MAX(VERSION, 2) == 3);
  check += (IS_EVEN(8) == 1);

  /* #elif chain picks the matching branch */
  check += (CHOSEN == 20);

  /* quote-form include, resolved relative to this file */
  check += (INC_DIR_VALUE == 42);
  check += (INC_ADD(INC_DIR_VALUE, 8) == 50);

  /* #if: arithmetic, precedence, comparison, logical */
#if 2 + 3 * 4 == 14
  check += 1;
#else
  check += 1000;
#endif

#if 1 != 2 && 0 == 0
  check += 1;
#else
  check += 1000;
#endif

#if defined(GUARD) && VERSION >= 3
  check += 1;
#else
  check += 1000;
#endif

  /* undefined identifiers in #if are 0 */
#if UNDEFINED_IDENT
  check += 1000;
#else
  check += 1;
#endif

#if defined(NOPE)
  check += 1000;
#endif

#if 0 || GUARD
  check += 1;
#endif

  /* #ifdef / #ifndef / #if !defined */
#ifdef GUARD
  check += 1;
#endif

#ifndef NOPE
  check += 1;
#endif

#if !defined(GUARD)
  check += 1000;
#endif

  /* #undef makes the name undefined again */
#undef FEATURE
#ifdef FEATURE
  check += 1000;
#endif
#if defined(FEATURE)
  check += 1000;
#else
  check += 1;
#endif

  /* nested conditionals */
#if VERSION > 0
#if VERSION > 1
#if VERSION > 2
  check += 1;
#endif
#endif
#endif

  /* an #else after a taken #if is legal and skipped */
#if 1
  check += 1;
#else
  check += 1000;
#endif

  /* dynamic macros */
  int l1 = __LINE__;
  int l2 = __LINE__;
  check += (l2 - l1 == 1);
  check += (l1 >= 1);
  check += (__COUNTER__ == 0);
  check += (__COUNTER__ == 1);
  check += (__COUNTER__ == 2);
#ifdef __STDC__
  check += (__STDC__ == 1);
#endif
  check += (__STDC_VERSION__ >= 199901);
  check += (strcmp(__FILE__, "tests/runpreproc.c") == 0);

  if (check != 26)
    return check;
  printf("runpreproc ok\n");
  return 0;
}
