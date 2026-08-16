/* GNU label-as-values: &&label is the address of a label inside the
 * same function (a void*), and goto *p jumps through it -- the
 * dispatch idiom behind threaded interpreters */

int main(void) {
  /* the basic shape: a small threaded dispatch through static data */
  static void *tbl[] = { &&a, &&b, &&c, &&done };
  int pc = 0;
  int acc = 0;

a:
  acc += 1;
  pc = 1;
  goto *tbl[pc];
b:
  acc += 2;
  pc = 2;
  goto *tbl[pc];
c:
  acc += 3;
  goto *tbl[3];
done:
  if (acc != 6) return 1;

  /* the address is a void*: stored, compared, dispatched */
  {
    void *p = &&jump_here;
    void *q = &&other;
    if (p == q) return 2;
    if (p != &&jump_here) return 3;
    if (p == (void *)0) return 4;
    goto *p;
  }
other:
  return 5;
jump_here:
  acc += 10;
  if (acc != 16) return 6;

  /* jumping forward into a nested block is fine: everything lives in
   * the function frame */
  {
    int inside = 7;
    void *targets[2] = { &&here, &&there };
    goto *targets[0];
  here:
    goto *targets[1];
  there:
    if (inside != 7) return 7;
  }

  /* local statics with label arrays work in blocks too */
  {
    static void *ops[4] = { &&m_end, &&m_inc, &&m_end, &&m_end };
    int it = 0;
m_inc:
    it++;
    goto *ops[it];
m_end:
    if (it != 2) return 8;
  }

  /* an address can be tested for nullness, and a second one taken
   * from the same spot */
  {
    void *mh = &&maybe_here;
    if (!mh) return 9;
    if (mh != (&&maybe_here)) return 10;
  }
maybe_here:

  /* typeof and _Generic agree the address is a void* */
  {
    typeof(&&typeof_here) tp = &&jump_out;
    if (tp != &&jump_out) return 11;
    if (_Generic((&&g1), void *: 1, default: 2) != 1) return 12;
g1:
  typeof_here:
    goto *tp;
jump_out:
    return 0;
  }
}