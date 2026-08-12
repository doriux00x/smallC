/* dump mode: union front-end coverage */
union value { int i; double d; char *s; };
union tagged { union value v; int tag; };

union value g_v = { 42 };
union tagged g_t;

union value pick(int sel);
union value pick(int sel) {
  union value v;
  if (sel)
    v.d = 1.5;
  else
    v.i = sel;
  return v;
}

int f(void) {
  union value v = g_v;
  int n = 0;
  if (v.d != 0.0)
    n += 1;
  g_t.v.i = 3;
  if (g_t.tag == 3)
    n += 2;
  union value arr[4];
  arr[0] = pick(1);
  n += sizeof(union value) + sizeof(arr) + arr[0].s != 0;
  return n;
}