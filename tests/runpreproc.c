/* preprocessor: object-like and function-like macros, #if/#ifdef/
 * #ifndef/#elif/#else/#endif with constant expressions and defined(),
 * #undef, quote-form #include resolved against the including file's
 * directory, # stringize, ## token paste, backslash-newline splicing,
 * variadic macros (... / __VA_ARGS__ with the GNU , ## __VA_ARGS__
 * comma swallow), zero-parameter macros, empty macro arguments,
 * GNU named variadic parameters ("args..."), __VA_OPT__ conditional
 * parts, the _Pragma operator, and the dynamic macros
 * __LINE__/__FILE__/
 * __COUNTER__/__STDC__. every check adds 1 to the counter; the fail
 * branches add 1000 so any mis-evaluated conditional blows the total. */

#include "inc/preproc.h"

#define VERSION 3
#define ZERO0() 0
#define E2(a, b) #a
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

  /* stringize: #x is the argument's spelling as a string */
#define STR(x) #x
  check += (strcmp(STR(hello), "hello") == 0);
  check += (strcmp(STR(1 + 2), "1 + 2") == 0);
  check += (strcmp(STR("q"), "\"q\"") == 0);
  check += (strcmp(STR('\n'), "'\\n'") == 0);

  /* token paste: the boundary tokens fuse, parameters used raw */
#define CAT(a, b) a ## b
#define GLUE(pre, n) CAT(pre, n)
#define CHAIN(a, b, c) a ## b ## c
#define FOO_1 7
  int pa = 5;
  int pa_x = 7;
  int pax = 9;
  check += (CAT(12, 34) == 1234);
  check += (GLUE(pa, _x) == 7);      /* param substituted before CAT runs */
  check += (CHAIN(pa, x, ) == 9);    /* chained pastes, empty tail arg */
  check += (CAT(, 3) == 3);          /* empty left operand */
  check += (CAT(4, ) == 4);          /* empty right operand */
  check += (CAT(FOO_, 1) == 7);      /* pasted name is rescanned, expands */

  /* backslash-newline splicing joins lines before tokenizing */
  check += (1 + \
2 == 3);
#define CONT(x) (x) + \
  (x)
  check += (CONT(5) == 10);
  char *twolines = "fo\
o";
  check += (strcmp(twolines, "foo") == 0);

  /* variadic macros: the last parameter takes the rest of the args */
#define FIRST(a, ...) a
#define SUM(...) __VA_ARGS__ + 1
#define TAIL(a, ...) __VA_ARGS__
#define STRV(...) #__VA_ARGS__
#define NARGS(...) NARGS_(__VA_ARGS__, 4, 3, 2, 1, 0)
#define NARGS_(e1, e2, e3, e4, n, ...) n
#define DPRINTF(fmt, ...) printf(fmt, ## __VA_ARGS__)
  check += (FIRST(1, 2, 3) == 1);
  check += (FIRST(7) == 7);                    /* empty __VA_ARGS__ */
  check += (SUM(4 + 5) == 10);                 /* tail spliced into body */
  check += (TAIL(1, 2, 3) == 3);               /* all extra args pass through */
  check += (strcmp(STRV(1 + 2), "1 + 2") == 0);
  check += (strcmp(STRV(), "") == 0);          /* stringized empty */
  check += (NARGS() == 1);                     /* the count trick */
  check += (NARGS(7, 8) == 2);
  check += (NARGS(7, 8, 9) == 3);
  check += (DPRINTF("x%d", 5) == 2);           /* comma kept, args passed */
  check += (DPRINTF("y") == 1);                /* comma swallowed */
  check += (ZERO0() == 0);                     /* zero-parameter macro */
  check += (strcmp(E2(, 5), "") == 0);         /* empty argument stringized */

  /* __VA_OPT__: the content survives only when the variadic slice is
   * non-empty (C23, gcc 8+); it can sit anywhere in the list, hold
   * # stringize, ## paste, params and __VA_ARGS__ itself, and pairs
   * with the GNU , ## __VA_ARGS__ comma swallow without doubling */
#define MAYBE(...) __VA_OPT__(check += 1;)
  MAYBE();
  MAYBE(1);                                    /* only the non-empty call fires */
#define VEMPTY(...) __VA_OPT__()
  VEMPTY();
  VEMPTY(1);                                   /* empty content, both spellings */
#define VUSE2(a, ...) a __VA_OPT__(+ 1)
  check += (VUSE2(5) == 5);                    /* empty slice: no content */
  check += (VUSE2(5, 9) == 6);                 /* non-empty: content kept */
#define VTAIL(a, ...) a __VA_OPT__(,) __VA_ARGS__
  check += (VTAIL(5) == 5);                    /* empty: no comma, no tail */
  check += (VTAIL(5, 9) == 9);                 /* non-empty: the , 9 tail */
#define VSTRV(...) __VA_OPT__(#__VA_ARGS__)
  check += (strcmp(VSTRV(1 + 2), "1 + 2") == 0);
#define VV(...) __VA_OPT__(__VA_ARGS__)
  check += (VV(1, 2) == 2);                    /* __VA_ARGS__ inside the content */
#define VDP(fmt, ...) printf(fmt __VA_OPT__(,) ## __VA_ARGS__)
  check += (VDP("a%d", 3) == 2);               /* comma + args, no paste */
  check += (VDP("b") == 1);                    /* both the comma and args vanish */
#define VPL(...) 1 ## __VA_OPT__(2)
  check += (VPL() == 1);                       /* empty: paste skipped, left alone */
  check += (VPL(1) == 12);
#define VPR(...) __VA_OPT__(1) ## 2
  check += (VPR() == 2);                       /* empty: right operand alone */
  check += (VPR(1) == 12);
#define VB2(...) __VA_OPT__(1) ## __VA_OPT__(2)
  check += (VB2(1) == 12);                     /* the two contents paste */
#define VCONT(...) __VA_OPT__(1 ## 2)
  check += (VCONT(1) == 12);                   /* paste inside the content */
#define VSTART(...) __VA_OPT__(check += 1;) 3
  VSTART();                                    /* group at the list start */
  VSTART(1);                                   /* only this one increments */
#define XNARGS(...) XNARGS_(__VA_OPT__(__VA_ARGS__, ) 5, 4, 3, 2, 1, 0)
#define XNARGS_(e1, e2, e3, e4, e5, n, ...) n
  check += (XNARGS() == 0);                    /* the __VA_OPT__ count idiom */
  check += (XNARGS(a) == 1);
  check += (XNARGS(a, b) == 2);

  /* _Pragma("..."): the C99 operator, gone after preprocessing like
   * the #pragma directive lines; the argument is macro-expanded and
   * must be one string literal, parens inside the string do not
   * count (modern gcc accepts unbalanced ones), and whitespace is
   * fine between the operator and its paren */
  _Pragma("GCC diagnostic push");
  _Pragma("GCC diagnostic ignored \"-Wswitch\"");
  _Pragma ("GCC diagnostic pop");
  _Pragma("pack(push, 1)");
  _Pragma("x(y");                              /* unbalanced: accepted */
#define PRAGSTR "pack(push, 1)"
  _Pragma(PRAGSTR);                            /* the argument expands */
#define PRG2(x) _Pragma(#x) check += 1;        /* the stringize idiom */
  PRG2(pack(push, 1));
#define PPSTMT _Pragma("GCC diagnostic push") check += 1;
  PPSTMT;

  /* GNU named variadic parameters: "args..." gives the slice a name,
   * taking over __VA_ARGS__'s slot; # stringize, ## paste, the comma
   * swallow and __VA_OPT__ all key off the parameter, not its
   * spelling, and __VA_ARGS__ inside such a macro is an ordinary
   * identifier (gcc warns the same way) */
#define VNAMED(a, args...) a + args
#define VONLY(args...) args
#define VUNUSED(a, args...) a
#define VSTR(args...) #args
#define VDP2(fmt, args...) printf(fmt, ## args)
#define VPAS(x, rest...) x ## rest
#define VOPT2(a, args...) a __VA_OPT__(+ 1)
  check += (VNAMED(1, 2) == 3);
  check += (VNAMED(1, 2, 3) == 3);        /* 1 + 2, 3: the comma wins */
  check += (VONLY(4, 5) == 5);            /* the whole list is the slice */
  check += (VNAMED(9, 10) == 19);
  check += (strcmp(VSTR(1 + 2), "1 + 2") == 0);
  check += (VDP2("x%d", 3) == 2);         /* comma kept, args passed */
  check += (VDP2("y") == 1);              /* comma swallowed, slice empty */
  check += (VPAS(12, 34) == 1234);
  check += (VOPT2(5) == 5);               /* empty slice: __VA_OPT__ off */
  check += (VOPT2(5, 9) == 6);            /* non-empty: __VA_OPT__ on */
  check += (VONLY(7) == 7);               /* single-token slice */
  check += (VUNUSED(8) == 8);             /* the named vararg may stay unused */

  if (check != 85)
    return check;
  printf("runpreproc ok\n");
  return 0;
}
