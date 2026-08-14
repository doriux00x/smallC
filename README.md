# smallC

A small C compiler for x86-64 Linux, written in C. It compiles a useful
subset of C to real x86-64 assembly and is meant to stay small: the whole
binary is currently about 250 KB on disk.

The compiler is one executable, `smallcc`. It does not use libc or LLVM
or anything like that. It is a from-scratch compiler: lexer, parser, type
checking, and x86-64 codegen. The generated assembly is assembled and
linked with your system `cc`.

## What works

The language subset grows all the time. As of now it handles:

- Integers: char, short, int, long and long long, with signed and
  unsigned, and the C99 literal suffixes `U`, `L`, `LL` (in either
  order and case; `1.5L` long doubles ride on double)
- _Bool, with proper semantics: any nonzero value stored into one
  becomes 1, so `_Bool b = 7;` gives you a 1. `bool`, `true` and
  `false` are not built in; `#define bool _Bool` and so on if you
  want them.
- A small preprocessor: object-like and function-like `#define` and
  `#undef`, with `#` stringize and `##` token paste, variadic macros
  (`...` / `__VA_ARGS__`, including the GNU `, ## __VA_ARGS__` comma
  swallow), `#include` in "..." (resolved against the including
  file's directory) and <...> form with `-I` search paths, and `#if`
  / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif` with constant
  expressions, `defined()`, and the dynamic macros `__LINE__`,
  `__FILE__`, `__COUNTER__`, `__STDC__`, `__STDC_VERSION__`.
  Backslash-newline splicing works everywhere the standard does it:
  between tokens, in comments, and inside string and character
  literals.
- Floating point: float and double, with SSE codegen, and hex float
  literals (`0x1.8p3`, `0x1p4`) with the f/L suffixes
- Pointers, arrays, and full declarator grammar (function pointers
  included)
- Struct, union, and enum types
- Flexible array members: the last member of a struct may be an
  incomplete array (`int a[]`); it adds nothing to `sizeof`
- Bit-fields (`int x : 3`), including unnamed and zero-width
  alignment markers; bit-field initializers are rejected
- typedef
- The const qualifier
- The volatile qualifier, in all positions (`volatile int`,
  `int volatile`, `volatile int *p`, `int *volatile p`). The
  backend never elides, reorders, or caches memory accesses, so
  volatile needs no special code generation; it is accepted and
  stored on the type so `volatile` code compiles as written.
- static and extern storage classes
- The register storage class, on locals and parameters. It is a
  hint, and the backend ignores it: every local already gets a
  stack slot and stays in memory only on calls, which is what
  register asks for anyway. Taking the address of one is
  accepted too, where C would reject it.
- switch / case / default, including case labels hidden inside
  blocks and branches, and constant expressions in case labels
- goto and statement labels, forward or backward, into and out of
  blocks
- Initializers: scalars, arrays, strings, structs, and brace
  initializers with zero-filling
- C99 designated initializers: `.member = v`, `[idx] = v`, chains
  like `.a.b[2] = v`, out-of-order members, and flexible arrays
  sized by the largest designator index
- C99 compound literals: `(int[]){1, 2, 3}` and
  `(struct point){1, 2}` create an anonymous initialized object at
  block scope, a fresh one on every evaluation, usable as an lvalue
- Struct and union values passed by value and returned by value
- Variadic functions, with `va_list`, `va_start`, `va_arg`, `va_end`
  and `va_copy`. `...` parameters ride the System V register save
  area, so a compiler-built `va_list` can even be handed to libc's
  `vprintf`
- Function definitions, prototypes, and calls with the System V
  calling convention
- The usual operators: arithmetic, comparisons with automatic
  promotion, logical, bitwise, casts, sizeof, assignment operators,
  ++/--, ternary, the comma operator with its sequence point, member
  access `.` and `->`, indexing

Nothing in the compiler is generated or bootstrapped; it is
self-written C compiled by your system compiler.

## What does not work

Known gaps, in no particular order:

- No VLA.
- Very small preprocessor: object-like and function-like `#define`
  with `#` stringize and `##` paste, variadic macros
  (`...`/`__VA_ARGS__`, with the GNU `, ## __VA_ARGS__` comma
  swallow), `#include` in quote and <...> form with `-I` search
  paths, and `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif` with
  constant expressions and `defined()`. But no `#pragma` handling
  beyond skipping, and the standard headers are not shipped; declare
  the few libc functions you use by hand, as the tests do.
- Only 64-bit x86 (System V ABI, Linux/ELF). No Windows, no ARM,
  no 32-bit.
- Global float/double initializers must be constant expressions,
  same as C requires.
- Integer literals are read as 32-bit quantities, so a value beyond
  `0xffffffff` cannot be spelled directly; build it with a shift
  (`1UL << 40`), which is what the `L` suffix helps with.

If something is missing that you need, the parser is small and the
features above show how each piece fits together, so adding one is
usually a day's work. Read the tests first.

## Building

You need a C compiler and make(1). Any of gcc or clang works. There
are no third-party dependencies, no configure step, no cmake, no
installed headers beyond the C standard library.

Plain build:

    make

This produces `smallcc` in the repo root and object files in `build/`.

Package installs by distro, if you do not already have a toolchain:

Gentoo:

    emerge --ask sys-devel/gcc sys-devel/make

Arch and derivatives (Artix, EndeavourOS, etc.):

    pacman -S base-devel

Debian, Ubuntu, Mint, and other apt-based distros:

    apt install build-essential

Fedora, RHEL, CentOS Stream, Rocky, AlmaLinux:

    dnf groupinstall "Development Tools"

openSUSE (Tumbleweed and Leap):

    zypper install -t pattern devel_basis

Alpine and other musl distros:

    apk add build-base

From there the build is always the same two commands:

    make
    make test

## Using it

Compile a single .c file to assembly:

    ./smallcc yourfile.c

The assembly lands in `build/yourfile.s`. Several files compile in
one run, each to its own `build/<base>.s` (basenames must not
clash):

    ./smallcc one.c two.c

Assemble and link with your system compiler (one or many files):

    cc -no-pie build/a.s build/b.s -o program

Then run it:

    ./build/yourfile

Two debug flags, mostly useful while developing the compiler itself:

    ./smallcc -a tests/switch.c   # dump the AST
    ./smallcc -t tests/lexer.c    # dump the token stream

## Tests

`make test` runs the whole suite:

- Dump tests feed the parser and print the AST or tokens; they only
  exercise the front end.
- Run tests are real programs. The compiler compiles them to
  assembly, your system cc assembles and links the result, and the
  program runs and must exit with status 0.

The run tests double as feature demos. `tests/rungoto.c` covers the
goto features, `tests/runstruct.c` the struct and union handling,
`tests/runbit.c` the bit-fields, `tests/runfloat.c` the floating
point, and so on.

`make size` prints the binary size; the informal budget is to keep
the whole compiler under 2 MB.

## Layout

    src/main.c      driver, flag handling, AST dump
    src/lexer.c     tokenizer
    src/parser.c    parser, type checker, initializer handling
    src/ast.c       AST node and type constructors
    src/codegen.c   x86-64 code generation
    src/util.c      allocators, error reporting
    tests/          parser dumps and runnable programs

The histories are in git. Each commit message names the feature it
adds, and the test for it lands in the same commit.

## Writing tests

A run test is a C file with a `main` that returns 0 on success (the
exit code is the verdict). Add it to the `test:` target in the
Makefile run loop; the compiler picks its name up via `tests/$t.c`.
There is no test framework, they are all just programs.