# smallC

A small C compiler for x86-64 Linux, written in C. It compiles a
useful subset of C to real x86-64 assembly. The whole thing is a
single executable, `smallcc`, about 460 KB on disk.

No LLVM, no borrowed front end, nothing clever: this is a from-scratch
compiler — lexer, preprocessor, parser, type checker, codegen — and
the only thing it leans on is your system `cc` to assemble and link
the assembly it prints. The language is mostly C99 with bits of C11
and C23 bolted on where they were fun or where real code trips over
them: variable-length arrays, `_Static_assert`, `__VA_OPT__`,
designated initializers, that sort of thing.

The point is that you can actually read it. Each stage is one file,
the parser is a couple thousand lines, and every feature commits with
its test, so the repo doubles as an answer to the question almost
every C programmer has wondered about once: what does a compiler
actually do, line by line.

## Demo

Say hello, the way this project says hello:

```c
/* no headers shipped; declare the one libc function you use */
int printf(const char *, ...);

int fib(int n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(void) {
  printf("fib(10) = %d\n", fib(10));
  return 0;
}
```

    ./smallcc fib.c
    cc -no-pie build/fib.s -o fib
    ./fib

    fib(10) = 55

The compiler writes `build/fib.s`; from there it is your system
toolchain doing its usual job.

## What works

- Integers: char, short, int, long and long long, signed and
  unsigned, with the C99 literal suffixes `U`, `L`, `LL` in either
  order and case. Integer literals read the full 64-bit value and
  type themselves down gcc's ladder (decimal: int, long, unsigned
  long; hex/octal: int, unsigned, long, unsigned long), so
  `0xFFFFFFFFFFFFFFFFUL` is `18446744073709551615` and
  `(int)0xFFFFFFFF` is -1, both exactly as gcc computes. A literal
  past 64 bits is an error instead of gcc's silent wrap. `1.5L`
  long doubles ride on double.
- `_Bool` with the semantics C actually demands: any nonzero value
  stored into one becomes 1, so `_Bool b = 7;` gives you a 1.
  `bool`, `true` and `false` are not built in — `#define bool _Bool`
  if you want them.
- A small but serious preprocessor: object-like and function-like
  `#define` and `#undef`, `#` stringize and `##` token paste
  (including zero-parameter macros and empty macro arguments),
  variadic macros with `...`/`__VA_ARGS__`, the GNU comma-swallow
  form `, ## __VA_ARGS__`, the GNU named form (`args...`), and the
  C23 `__VA_OPT__(...)` conditional part. The command line takes
  `-D NAME[=VALUE]` and `-U NAME` like gcc: the name is the leading
  identifier run and the rest (with or without the `=`) is the
  replacement list, tokenized like a `#define` body; `-U` and
  `#undef` can even revoke a predefined macro, and the macro table
  survives across input files, so the flags apply to every file
  compiled afterwards. Redefining a macro warns (gcc's default
  `-Wmacro-redefined`) unless the new parameter names and body are
  the same token sequence — spacing does not count — and the note
  points at the previous definition (`<command-line>` for `-D`);
  `#undef` first and there is nothing to warn about. `-include FILE` preprocesses `FILE` at the
  very start of every translation unit, as if an `#include "FILE"`
  led the source; it shares the macros and `#pragma once` registry
  of the unit that follows it. `-E` runs the preprocessor alone,
  printing the result to stdout, and `-M` (or `-MM`; nothing here
  counts as a system header) writes gcc's make dependency rule: the
  source leads the list, every file preprocessed for it follows in
  open order deduped, wrapped with ` \` continuations at gcc's
  column. `-MP` appends a phony `<dep>:` rule per dependency (so a
  deleted header does not break `make` mid-build); `-MD`/`-MMD`
  write the same rule to `build/<base>.d` as a side effect of a
  normal compile instead of taking the run over. `#include` in `"..."` form
  resolves against the including file's directory first and then the
  `-I` dirs; the `<...>` form only searches the `-I` dirs. The name
  is macro-expanded first, as gcc does, so
  `#define HDR "x.h"` + `#include HDR` works. `#include_next` (gcc)
  resumes the search one directory past the one that produced the
  file being read: the including file's own directory and every -I
  slot up to and including the current file's are skipped, so layered
  header trees (`chain.h` in dir 1 handing over to `leaf.h` in dir 2)
  can append to a shadowed header without recursing into it. `#if` /
  `#ifdef` / `#ifndef` / `#elif` / `#elifdef` / `#elifndef` / `#else` /
  `#endif` evaluate
  constant integer expressions with `defined()`, `__has_include`,
  `__has_attribute(name)` and `__has_builtin(name)` — the latter two
  answer 1 only for what the compiler really implements (the
  `packed`/`aligned`/`noreturn` attributes, the fold-at-parse
  builtins), and all three count as defined for `#ifdef`/`defined()`
  the way gcc reports them, and constants that are single-token
  macros. `#line N ["file"]`
  renumbers the file like gcc does, and `_Pragma` pipes through to
  the skipped-pragma path. `#warning msg` prints gcc's own line
  format (`path:line:col: warning: #warning msg [-Wcpp]`) and keeps
  compiling, and it stays silent inside a skipped branch.
  `#pragma message("text")` is gcc's build-time banner: the operand
  is macro-expanded and adjacent strings concatenate, a
  `path:line:col: note: '#pragma message: text'` goes to stderr,
  and the compile goes on; a malformed operand draws gcc's
  "expected a string" warning instead. `#pragma once` (and its `_Pragma("once")`
  form) compiles a file once per translation unit: the file is
  registered under its canonicalized path, so a re-include through
  a different path spelling still skips it. `#pragma GCC poison NAME...`
  flags each NAME like gcc: any later use, `#define`, or appearance
  in `defined()`, `#ifdef`/`#ifndef`/`#elifdef`/`#elifndef` — but not
  inside a skipped branch — is an error ("attempt to use poisoned
  \"NAME\""), a guardrail for retired interfaces. `#pragma GCC poison NAME...`
  flags each NAME like gcc: any later use, `#define`, or appearance
  in `defined()`, `#ifdef`/`#ifndef`/`#elifdef`/`#elifndef` — but not
  inside a skipped branch — is an error ("attempt to use poisoned
  \"NAME\""), a guardrail for retired interfaces.
  any spelling — `..` components, `./`, symlinks — is skipped, and
  `__has_include` still reports it. The always-on macros are `__LINE__`,
  `__FILE__`, `__FILE_NAME__` (the basename, gcc 12's
  tree-independent spelling), `__INCLUDE_LEVEL__` (how deep the
  current file sits in the include stack: 0 at the top of the unit,
  1 in the first #include or -include, and so on), `__COUNTER__`,
  `__DATE__` and `__TIME__` (frozen when a
  translation unit starts preprocessing, with gcc's exact formats -
  the Makefile greps the stamped date out of an -E run and compares
  it with `date`), `__TIMESTAMP__` (each file's own mtime in
  ctime's format, cross-checked against `date -r FILE`),
  `__STDC__`, `__STDC_VERSION__`, `__STDC_HOSTED__`,
  the gcc
  identification set `__GNUC__`/`__GNUC_MINOR__`/`__GNUC_PATCHLEVEL__`/
  `__GNUC_STDC_INLINE__`/`__VERSION__` (mirrored from the gcc that
  builds smallcc, so glibc's `__GNUC_PREREQ` gates are satisfied), and
  the platform macros `__x86_64__`/`__amd64`/`__amd64__` and
  `__linux__`/`__linux`. `__STDC_VERSION__` stays pinned at 199901,
  the C99 floor this compiler targets. Backslash-newline splicing works
  everywhere the standard does it: between tokens, in comments, and
  inside string and character literals.
- Floating point: float and double with SSE codegen, hex float
  literals (`0x1.8p3`) with the `f`/`L` suffixes.
- The C99 escape sequences in string and character literals, octals
  to three digits, hex escapes to however many (truncated to 8 bits,
  as gcc does). A numeric escape above 127 in a character literal is
  a signed execution char, so `'\xff'` is -1 on x86-64, exactly as
  gcc computes it.
- Pointers, arrays, and the full declarator grammar, function
  pointers included.
- Variable-length arrays: `int a[n]` with any dimension expression,
  carved out of the stack where they are declared. `sizeof` on the
  declared variable is the size its declaration captured, as gcc
  does; slices, strides, and nested dimensions compute at run time.
  No initializers, no `static` VLAs, no VLA struct members; a VLA
  parameter decays to a pointer, as in C.
- Struct, union, and enum types, passed and returned by value.
- `_Static_assert(cond, "msg")` at top level and inside functions;
  a zero condition is a compile-time error carrying the message. The
  condition must be constant, so VLA `sizeof` is rejected.
- Flexible array members: a trailing `int a[]` adds nothing to
  `sizeof`.
- Bit-fields, including unnamed and zero-width alignment markers.
  Bit-field initializers are rejected.
- typedef, const, and volatile in every position. The backend never
  elides, reorders, or caches memory accesses, so volatile needs no
  special codegen: volatile code compiles as written.
- static and extern; register is accepted as the hint it always was
  (every local already lives in a stack slot and stays there except
  across calls). Taking the address of a register variable is
  accepted too, where C would reject it.
- switch / case / default, even with case labels hiding inside
  blocks, and constant expressions as case values. goto and labels,
  forward or backward, into and out of blocks.
- Initializers: scalars, arrays, strings, structs, brace lists with
  zero-filling. C99 designated initializers (`.member = v`,
  `[idx] = v`, chains like `.a.b[2] = v`, out of order, and flexible
  arrays sized by the largest designator index). C99 compound
  literals create an anonymous initialized object at block scope, a
  fresh one per evaluation, usable as an lvalue.
- Variadic functions with `va_list`, `va_start`, `va_arg`, `va_end`,
  `va_copy`. `...` parameters ride the System V register save area,
  so a compiler-built `va_list` can be handed straight to libc's
  `vprintf`.
- The gcc-compat builtins people actually use: `__builtin_offsetof`,
  `__builtin_expect`, `__builtin_unreachable`, `__builtin_constant_p`,
  `__builtin_types_compatible_p`, `__builtin_choose_expr`.
- The usual operators: arithmetic with automatic promotion,
  comparisons, logical, bitwise, casts, `sizeof`, assignment
  operators, `++`/`--`, ternary, the comma operator with its
  sequence point, `.` and `->`, indexing.

The compiler is self-written C built by your system compiler; nothing
here is generated or bootstrapped (well, one thing is — see
"self-hosting" below).

## What does not work

In no particular order, and knowingly:

- VLA limits: no initializers, no `static` VLAs, no VLA struct
  members (the works list above spells out what does work).
- Bit-field initializers are still rejected.
- Raw `#pragma` directives are skipped. The `_Pragma` operator is
  implemented, which is the form that matters inside macros.
- The standard headers are not shipped. Declare the few libc
  functions you use by hand, the way the tests do.
- Only 64-bit x86: System V ABI, Linux/ELF. No Windows, no ARM, no
  32-bit anything.
- No debug info. The assembly is plain; gdb will show you symbols,
  not line tables.

If something is missing that you need, the parser is small and the
features above show how each piece fits together, so adding one is
usually a day's work. Read the tests first.

## Building

You need a C compiler and make(1); gcc or clang both work. There are
no third-party dependencies, no configure step, no cmake, no installed
headers beyond the C standard library.

    make

This produces `smallcc` in the repo root and object files in `build/`.
Package installs by distro, if you do not already have a toolchain:

- Gentoo: `emerge --ask sys-devel/gcc sys-devel/make`
- Arch and derivatives (Artix, EndeavourOS, ...): `pacman -S base-devel`
- Debian, Ubuntu, Mint, and other apt-based distros:
  `apt install build-essential`
- Fedora, RHEL, CentOS Stream, Rocky, AlmaLinux:
  `dnf groupinstall "Development Tools"`
- openSUSE (Tumbleweed and Leap): `zypper install -t pattern devel_basis`
- Alpine and other musl distros: `apk add build-base`

From there the build is always the same two commands:

    make
    make test

### Self-hosting

The compiler compiles itself:

    make selftest

smallcc compiles its own six source files, your cc assembles them, the
result is smallcc2; smallcc2 does the same to produce smallcc3, and
the two must be byte-for-byte identical. Then the whole test suite
runs against smallcc2. One honest asterisk: `util.o` is still built
by the host cc, because the util unit still needs real `va_list`.
Self-hosting, with training wheels.

## Using it

Compile one file to assembly:

    ./smallcc yourfile.c

The assembly lands in `build/yourfile.s`. Several files compile in
one run, each to its own `build/<base>.s` (basenames must not clash):

    ./smallcc one.c two.c

`-I dir` adds an include search path (repeatable). Assemble and link
with your system compiler:

    cc -no-pie build/a.s build/b.s -o program

Two debug flags, mostly for developing the compiler itself:

    ./smallcc -a tests/switch.c   # dump the AST
    ./smallcc -t tests/lexer.c    # dump the token stream

`-E` prints the preprocessed translation unit to stdout (no assembly,
no `#` markers), matching `gcc -E -P` byte for byte on its own test
files - the Makefile `test` target diffs the two and then recompiles
the printed text, so an -E regression is a failed diff:

    ./smallcc -E -D FLAG=9 tests/rune.c

One translation unit is read, tokenized, preprocessed, parsed,
resolved, and code-generated; every stage rewinds its own state, so
several files in one invocation are just that pipeline several times
in a row.

## Tests

`make test` runs the whole suite:

- Dump tests feed the parser and print the AST or tokens; they only
  exercise the front end.
- Run tests are real programs. The compiler compiles them to
  assembly, cc assembles and links the result, and the program runs
  and must exit 0.

The run tests double as feature demos. `tests/rungoto.c` is the goto
feature, `tests/runstruct.c` the struct and union handling,
`tests/runbit.c` the bit-fields, `tests/runfloat.c` the floating
point, `tests/runpreproc.c` the preprocessor, and so on. The informal
budget is to keep the whole compiler well under 2 MB; right now it is
about 460 KB.

## Layout

    src/main.c      driver, flags, AST dump
    src/lexer.c     tokenizer
    src/preproc.c   preprocessor, operating on the lexer's token
                    stream: macro expansions are spliced back into
                    one flat chain, so the parser never sees
                    anything but a single token sequence
    src/parser.c    parser, type checker, initializer handling,
                    the __builtin_* forms
    src/ast.c       AST node and type constructors
    src/codegen.c   the entire backend, straight to assembly text
    src/util.c      allocators, error reporting
    tests/          parser dumps and runnable programs

The histories are in git. Each commit message names the feature it
adds, and the test for it lands in the same commit.

## Writing tests

A run test is a C file with a `main` that returns 0 on success; the
exit code is the verdict. Add it to the `test:` target in the
Makefile run loop, and the compiler picks it up via `tests/$t.c`.
There is no test framework; they are all just programs.

## What's next

In no particular order, all of it rooted in the gaps above:

- Bit-field initializers.
- VLA initializers and `static` VLAs.
- DWARF line tables, since the codegen is already real enough that
  breakpoints would work.

Non-goals, so nobody has to ask: Windows, ARM, 32-bit targets, a
shipped libc, and replacing your system toolchain. smallC stays small;
that is the point.