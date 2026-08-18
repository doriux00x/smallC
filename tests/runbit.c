// bit-fields: packing, sign extension, wraparound, ++/--, compound
// assignment, _Bool normalization, layout rules
struct flags {
  int a : 3;
  int b : 5;
  int c : 24;
};

// structs containing bit-fields round-trip by value (memcpy copies
// the storage units whole)
struct k { int a : 5; int b : 6; };

struct k mk(void) {
  struct k r;
  r.a = -7;
  r.b = 30;
  return r;
}

int sum(struct k k) {
  return k.a + k.b;
}

// a global struct with bit-fields
struct g { int a : 4; int b : 4; } g;

int main() {
  struct flags f;
  f.a = 0;
  f.b = 0;
  f.c = 0;
  if (sizeof(struct flags) != 4) return 1;

  f.a = 3;
  if (f.a != 3) return 2;
  if (f.b != 0) return 3;
  if (f.c != 0) return 4;

  // stores wrap to the field width; a 3-bit signed field turns
  // 5 into -3 and -5 into 3
  f.a = 5;
  if (f.a != -3) return 5;
  f.a = -4;
  if (f.a != -4) return 6;
  f.a = -5;
  if (f.a != 3) return 7;

  // b occupies bits 3..7 of the same unit; c bits 8..31
  f.b = 15;
  if (f.b != 15) return 8;
  if (f.a != 3) return 9;
  f.a = -4;
  if (f.b != 15) return 10;
  if (f.a != -4) return 11;
  f.b = 17;                      // 10001b: the sign bit is set, so -15
  if (f.b != -15) return 12;
  f.c = 1000000;
  if (f.c != 1000000) return 13;
  f.a = 2;
  if (f.c != 1000000) return 14;
  f.c = -1000000;
  if (f.c != -1000000) return 15;

  // the assignment expression's value is the stored field value
  int x = (f.a = 3) + 1;
  if (x != 4) return 16;
  f.b = f.c = 5;
  if (f.b != 5) return 17;

  // compound assignment loads, combines and re-masks the field
  struct flags q;
  q.a = 0;
  q.b = 0;
  q.c = 0;
  q.a = 3;
  q.a += 2;
  if (q.a != -3) return 18;
  q.b = 4;
  q.b *= 3;
  if (q.b != 12) return 19;
  q.b <<= 2;                    // 48 wraps to 16, signed: -16
  if (q.b != -16) return 20;
  q.c = 5;
  q.c -= 10;
  if (q.c != -5) return 21;

  // ++/--; postfix yields the old value
  struct flags p;
  p.a = 0;
  p.b = 0;
  p.c = 0;
  p.a = 2;
  if (p.a++ != 2) return 22;
  if (p.a != 3) return 23;       // 3 = 011b still fits signed 3 bits
  if (++p.a != -4) return 24;    // 4 = 100b: the sign bit, -4
  if (--p.a != 3) return 25;     // -5 = 101b wraps to +3
  if (p.a-- != 3) return 26;
  if (p.a != 2) return 27;

  // unsigned fields never sign-extend
  struct { unsigned a : 3; } u;
  u.a = 5;
  if (u.a != 5) return 28;
  u.a = -1;
  if (u.a != 7) return 29;

  // _Bool fields store normalized 0/1
  struct { _Bool b : 1; } bf;
  bf.b = 2;
  if (bf.b != 1) return 30;
  bf.b = 0;
  if (bf.b != 0) return 31;
  bf.b = 1;
  if (bf.b != 1) return 32;

  // a,b,c pack into one int unit; the plain char lands after it
  union {
    struct { int a : 3; int b : 5; int c : 5; char d; } s;
    char bytes[8];
  } l;
  l.s.a = 0;
  l.s.b = 0;
  l.s.c = 0;
  l.s.d = 'z';
  if (sizeof(l) != 8) return 32;
  l.s.a = 5;                       // 101b in bits 0-2
  if ((l.bytes[0] & 7) != 5) return 33;
  l.s.b = 17;                      // bits 3-7
  if ((l.bytes[0] & 7) != 5) return 34;
  if (((l.bytes[0] >> 3) & 31) != 17) return 35;
  l.s.c = 31;                      // 5 bits at bits 8-12
  if ((l.bytes[1] & 31) != 31) return 36;
  if (l.bytes[4] != 'z') return 37;
  if (l.s.d != 'z') return 38;

  // a field that does not fit the remaining bits starts a new unit
  union {
    struct { int a : 29; int b : 4; } s;
    int words[2];
  } o;
  o.s.a = 0;
  o.s.b = 0;
  if (sizeof(o) != 8) return 39;
  o.s.a = 536870911;               // all 29 bits set
  if (o.words[0] != 536870911) return 40;
  o.s.b = 7;
  if (o.words[0] != 536870911) return 41;
  if (o.words[1] != 7) return 42;  // b lives in the second unit
  o.s.a = -268435456;              // -2^28 = the 29-bit minimum
  if (o.words[0] != 268435456) return 43;  // raw: 0x10000000
  if (o.s.a != -268435456) return 44;

  // the anonymous zero-width field forces a fresh unit
  union {
    struct { int a : 3; int : 0; int b : 3; } s;
    int words[2];
  } z;
  z.s.a = 0;
  z.s.b = 0;
  if (sizeof(z) != 8) return 44;
  z.s.a = 2;
  if ((z.words[0] & 7) != 2) return 45;
  z.s.b = -3;                  // 101b signed: -3, in the fresh unit
  if ((z.words[0] & 7) != 2) return 46;
  if ((z.words[1] & 7) != 5) return 47;

  // char-sized units pack 8 bits; a different base type starts a
  // fresh unit at the new alignment
  union { struct { char a : 3; char b : 3; } s; char byte; } c2;
  c2.s.a = 0;
  c2.s.b = 0;
  if (sizeof(c2) != 1) return 48;
  c2.s.a = 5;                      // 101b signed: -3
  if ((c2.byte & 7) != 5) return 49;
  if (c2.s.a != -3) return 50;
  c2.s.b = 2;
  if (((c2.byte >> 3) & 7) != 2) return 51;
  if (c2.s.a != -3) return 52;
  union {
    struct { char a : 3; int b : 3; } s;
    char bytes[8];
  } d;
  d.s.a = 0;
  d.s.b = 0;
  if (sizeof(d) != 8) return 53;
  d.s.a = 5;
  if (d.bytes[0] != 5) return 54;
  d.s.b = 2;
  if (d.bytes[4] != 2) return 55;  // b's unit starts at offset 4
  if (d.bytes[0] != 5) return 56;

  // long fields use the full 64-bit unit
  struct { long a : 40; long b : 24; } lg;
  lg.a = 0;
  lg.b = 0;
  if (sizeof(lg) != 8) return 57;
  lg.a = -0x8000000000 + 3;      // most negative 40-bit value + 3
  if (lg.a != -0x8000000000 + 3) return 58;
  lg.b = 0x7FFFFF;               // 2^23 - 1
  if (lg.b != 0x7FFFFF) return 59;
  if (lg.a != -0x8000000000 + 3) return 60;

  // bit-fields in a union share the first bits
  union { int a : 3; int b : 1; } w;
  w.a = 3;
  if (w.b != -1) return 61;      // 1 bit signed: 1 = -1
  w.b = 0;
  if (w.a != 2) return 62;       // a = 010b = 2
  if (sizeof(w) != 4) return 63;

  // structs containing bit-fields round-trip by value (memcpy copies
  // the storage units whole)
  struct k k = mk();
  if (k.a != -7) return 64;
  if (k.b != 30) return 65;
  if (sum(k) != 23) return 66;

  // a global struct with bit-fields
  g.a = 5;
  g.b = -5;
  if (g.a != 5) return 67;
  if (g.b != -5) return 68;

  // unnamed fields pad without being readable
  struct { int a : 3; int : 4; int b : 5; } pad;
  pad.a = 0;
  pad.b = 0;
  if (sizeof(pad) != 4) return 69;
  pad.a = 1;
  pad.b = 2;
  if (pad.a != 1) return 70;
  if (pad.b != 2) return 71;

  // braced initializers: fields merge into their storage unit, both
  // as a temporary (runtime merges) and as a static (one .long)
  struct flags i1 = { 2, -3, 1000000 };
  if (i1.a != 2) return 72;
  if (i1.b != -3) return 73;
  if (i1.c != 1000000) return 74;
  if (i1.a != 2 || i1.b != -3 || i1.c != 1000000) return 75;

  // a short list zero-fills the rest of the unit without touching
  // the fields that got values
  struct { unsigned a : 3; unsigned b : 5; unsigned c : 8; } i2 = { 5 };
  if (i2.a != 5) return 76;
  if (i2.b != 0) return 77;
  if (i2.c != 0) return 78;
  i2.a = 5;
  if (i2.a != 5) return 79;
  if (i2.b != 0) return 80;

  // signedness and wraparound apply to initializer values too
  struct { int a : 4; int b : 4; } i3 = { 8, -8 };
  if (i3.a != -8) return 81;
  if (i3.b != -8) return 82;

  // designators target individual fields, in any order
  struct { unsigned a : 3; unsigned b : 5; } i4 = { .b = 17, .a = 6 };
  if (i4.a != 6) return 83;
  if (i4.b != 17) return 84;
  if (sizeof(i4) != 4) return 85;

  // a braced scalar for one field
  struct { unsigned a : 3; unsigned b : 5; } i5 = { {4}, 31 };
  if (i5.a != 4) return 86;
  if (i5.b != 31) return 87;

  // _Bool normalization in initializers
  struct { _Bool a : 1; _Bool b : 1; } i6 = { 2, 3 };
  if (i6.a != 1) return 88;
  if (i6.b != 1) return 89;

  // bit-fields nested inside arrays and member structs
  struct { unsigned a : 3; unsigned b : 5; } iarr[2] = { { 1, 2 }, { 7, 31 } };
  if (iarr[0].a != 1) return 90;
  if (iarr[0].b != 2) return 91;
  if (iarr[1].a != 7) return 92;
  if (iarr[1].b != 31) return 93;

  // a static inside a function initializes via the .data path; 29
  // in a signed 5-bit field wraps to -3
  static struct { int a : 3; int b : 5; } ist = { 3, 29 };
  if (ist.a != 3) return 94;
  if (ist.b != -3) return 95;

  // global braces
  struct { unsigned a : 4; unsigned b : 12; } ig = { 15, 4095 };
  if (ig.a != 15) return 96;
  if (ig.b != 4095) return 97;

  // an out-of-range value wraps to the field width in the unit
  struct { unsigned a : 3; unsigned b : 29; } i7 = { 9, 0x1FFFFFFF };
  if (i7.a != 1) return 98;
  if (i7.b != 0x1FFFFFFF) return 99;

  return 0;
}
