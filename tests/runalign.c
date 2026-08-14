/* _Alignof and _Alignas (C11 alignment): operator folding, object
 * alignment on locals/globals/members, and layout consequences */

_Alignas(16) int g16;            /* over-aligned global, .bss */
_Alignas(32) int g32[2] = {1, 2}; /* over-aligned initialized global */

int main(void) {
  /* _Alignof on scalars */
  if (_Alignof(char) != 1) return 1;
  if (_Alignof(short) != 2) return 2;
  if (_Alignof(int) != 4) return 3;
  if (_Alignof(long) != 8) return 4;
  if (_Alignof(float) != 4) return 5;
  if (_Alignof(double) != 8) return 6;
  if (_Alignof(int[4]) != 4) return 7;   /* arrays: the element's align */
  if (_Alignof(int *) != 8) return 8;
  if (_Alignof(void *) != 8) return 9;

  /* _Alignof on a struct: its own alignment */
  struct Al { char c; int i; };
  if (_Alignof(struct Al) != 4) return 10;
  if (sizeof(struct Al) != 8) return 11;

  /* _Alignof is a compile-time constant usable in declarations */
  int a4[_Alignof(int) == 4 ? 3 : 1];
  if (sizeof(a4) != 12) return 12;

  /* expression form: _Alignof of a variable respects _Alignas */
  _Alignas(16) int x;
  if (_Alignof(x) != 16) return 13;
  if (((unsigned long)&x) % 16 != 0) return 14;

  /* over-aligned locals land 16-aligned on the stack */
  _Alignas(16) long buf[4];
  if (((unsigned long)buf) % 16 != 0) return 15;
  /* and the natural one is untouched */
  int y;
  if (((unsigned long)&y) % 8 != 0) return 16;

  /* over-aligned pointer object: the pointer, not the pointee */
  _Alignas(16) int *p;
  if (((unsigned long)&p) % 16 != 0) return 17;
  _Alignas(16) int ih[4];
  p = ih;
  for (int k = 0; k < 4; k++)
    p[k] = k + 1;
  if (ih[0] != 1 || ih[3] != 4) return 18;

  /* _Alignas specifier can trail the type ("int _Alignas(16) w") */
  int _Alignas(16) w;
  if (((unsigned long)&w) % 16 != 0) return 19;

  /* a power-of-two check and nonzero values held */
  _Alignas(8) short s8;
  if (((unsigned long)&s8) % 8 != 0) return 20;
  _Alignas(1) char c1;
  (void)c1;

  /* globals, .bss and initialized, are aligned too */
  if (((unsigned long)&g16) % 16 != 0) return 21;
  if (((unsigned long)&g32) % 32 != 0) return 22;
  if (g32[0] != 1 || g32[1] != 2) return 23;

  /* a struct with an over-aligned member: the member sits at a
   * 16-aligned offset and the whole struct takes the alignment */
  struct S { char tag; _Alignas(16) int i; };
  if (_Alignof(struct S) != 16) return 24;
  if (sizeof(struct S) != 32) return 25;
  struct S sv;
  if (((unsigned long)&sv.i) % 16 != 0) return 26;
  if ((char *)&sv.i - (char *)&sv != 16) return 27;

  /* _Alignof on the struct's over-aligned member type */
  if (_Alignof(struct S) != 16) return 28;

  /* a union with an over-aligned member takes the larger alignment */
  union U { char c; _Alignas(16) double d; };
  if (_Alignof(union U) != 16) return 29;
  if (sizeof(union U) != 16) return 30;
  union U uv;
  uv.d = 1.5;
  if (uv.d != 1.5) return 31;
  if (((unsigned long)&uv.d) % 16 != 0) return 32;

  /* _Alignas interacts with brace initialization */
  _Alignas(16) int ai[4] = {5, 6, 7, 8};
  if (ai[1] != 6 || ai[3] != 8) return 33;
  if (((unsigned long)ai) % 16 != 0) return 34;

  printf("runalign ok\n");
  return 0;
}