# IR backend: stop printing assembly from codegen.c

## the problem

codegen.c is 3.3k lines of walking the resolved AST and fprintf'ing
AT&T text directly. expressions are evaluated with a stack machine:
every expression leaves its answer in %rax, binary operators spill
the right operand to the stack and recombine in %rdi, calls push all
six arguments and pop them into registers. it works - it compiles
itself, twice - but it is a dead end:

- no register allocation. the stack is the register file and the
  spills are the program. every statement boundary is a memory round
  trip, and nothing is left to optimize later
- control flow is string emission: break/continue stacks, &&label
  tables, switch jump tables and gotos are all woven into fprintf
  calls. nobody can inspect, reorder or reason about them
- floats are half-there on purpose (see the header comment). the fix
  has no home in a string printer
- a second target is not a port, it is a second codegen.c

the idea: the resolved tree lowers to a small linear IR of
instructions over virtual registers. assembly becomes a dumb printer
over the IR. everything else gets to exist.

## the IR

three things: instructions, basic blocks, functions.

an instruction is up to two register operands and an immediate, plus
a kind that decides how the printer spells it:

    typedef struct Insn Insn;
    struct Insn {
      Insn *next;
      int kind;              /* MOV, ADD, SUB, MUL, LOAD, STORE, CALL, J, ... */
      int dst, src1, src2;   /* virtual registers; -1 = absent */
      int imm;               /* immediates, displacements, labels */
      int use_xmm;           /* floats live in xmm registers only */
    };

    typedef struct Block Block;
    struct Block {
      Block *next;
      int id;                /* bb0, bb1, ... */
      Insn *head, *tail;     /* straight-line stretch */
      int term;              /* J, JNZ, RET ...; last insn doubles as terminator */
    };

    typedef struct IrFn IrFn;   /* one per function, plus a fake one
                                   for global data emission */
    struct IrFn {
      IrFn *next;
      char *name;
      Block *blocks;
      int nregs;             /* vregs are densed 0..nregs-1 */
      int frame;             /* stack slots stay rbp-relative, as today */
    };

no SSA. virtual registers may be written more than once, exactly like
the AST values they come from. a renaming pass is a later commit and
the front end never has to care. types are not in the IR: the
resolve pass still runs first, and the printer re-derives what it
needs (movzbl vs movl vs movq) from the same info it uses today.

## worked example

    int f(int a, int b) { return a + b * 2; }

resolved AST -> IR (what -i will dump):

    @f:
    bb0:
      %r0 = mov %p0          ; parameters land in their own vregs
      %r1 = mov %p1
      %r2 = mul %r1, $2
      %r3 = add %r0, %r2
      store %ret_slot, %r3   ; the return slot, rbp-relative as always
      jmp ret_label          ; returns funnel through ret_label, as today

the point of the listing: bb0 is exactly the printable text, one
insn = one line, and nothing after the printer has any decisions
left to make.

IR -> asm stays byte-identical for as long as the printer mirrors
the current emission, which is the whole migration strategy.

## what stays

resolve() is untouched. it does the real work: symbol tables, type
inference, pointer arithmetic folding, ND_VAR -> Obj. the IR gets the
annotated tree.

the following all have strings woven into codegen today and must
survive the printer swap without a semantic step backwards:

- bitfields (load/store with shift+mask), and their -a dump
- VLA sizing: vla_size_expr evaluates a size tree at runtime
- the stdarg machinery: va_start/va_arg/va_end/va_copy, cur_va_off
- TLS: %fs + @tpoff, .tdata/.tbss
- GNU &&label: void* table in static data, goto *p, whole-compile
  label registry
- float constant pools (.LC0, .LCf0) and fconsts
- static function-locals emitted like globals after .text
- _Alignas, packed, the full layout circus
- statement expressions, _Generic, lvalue casts

## lowering sketch

the lowering is a stack of vregs per expression, the same discipline
the AST walk uses for %rax. every lower_expr returns the vreg holding
the value:

    static int lower_expr(Node *n, Block *b) {
      switch (n->kind) {
      case ND_NUM:   return mov_imm(b, n->val);
      case ND_VAR:   return load(b, obj_slot(n->var));
      case ND_BIN: {
        int l = lower_expr(n->lhs, b);
        int r = lower_expr(n->rhs, b);
        return bin(b, op_for(n->op), l, r);   /* mul before add, as today */
      }
      case ND_CAST:  ... /* same sign/width juggling the printer does */
      case ND_CALL:  ... /* same six-arg dance, same spill */
      default:       ...
      }
    }

    static void lower_stmt(Node *n, Block *b) {
      switch (n->kind) {
      case ND_RETURN:
        lower_expr(n->lhs, b);
        term(b, RET);
        return;
      case ND_IF: {
        Block *then = new_block(), *els = new_block(), *done = new_block();
        term(b, JZ, lower_expr(n->cond, b), then);
        lower_stmt(n->then, then);
        term(then, JMP, done);
        lower_stmt(n->els ? n->els : nop(), els);
        term(els, JMP, done);
        b = done;
        return;
      }
      ...  /* while/for/switch reuse the break/continue label stacks,
             only now they are Block* instead of int */
      }
    }

the printer:

    static const char *spell[IR_NKINDS] = { [IR_MOV]="mov", ... };

    static void print_block(Block *b) {
      for (Insn *i = b->head; i; i = i->next)
        fprintf(out, "  %s %s, %s\n", spell[i->kind], opname(i->src1), opname(i->dst));
    }

## commit plan

the whole point of the IR is that the compiler never goes through a
broken intermediate. every commit keeps `make test` and `make
selftest` green, and selftest stays byte-identical for as long as we
still self-host. gcc cross-checks stay for every new run test.

1. **the IR types and an expression-only dumper (-i)**: the structs
   above, plus lowering for straight-line expressions behind a new
   `-i` flag that prints the IR and exits. old codegen untouched.
   test: dump every run*.c, eyeball, and the self-hosted smallcc
   stays identical because nothing changed on its path.

2. **blocks and statement lowering**: control flow goes through
   blocks/terminators. the -i dump now covers whole functions.
   the old string emission is still the only thing that prints.

3. **the printer swap**: gen_expr/gen_stmt's fprintf calls become
   print_block. delete the string-spitting paths, keep resolve()
   and the layout code. the compiler output is byte-identical - this
   is the check. floats keep the half-there status; nothing regresses.

4. **registers, first honest attempt**: a trivial stack-slot
   allocator (every vreg owns an rbp slot, exactly today's cost),
   then liveness over blocks and a linear scan that keeps the
   hot operands in regs. the stack machine's spills vanish where
   the scan can prove them dead.

5. **the gaps become features**: floats/xmm everywhere, the 16-byte
   alignment FIXME, short-circuit &&/||, and whatever the run tests
   expose. each is now a normal commit instead of a string-printer
   surgery.

## done means

- `make test` green, `make selftest` green, smallcc2 == smallcc3
- `-i` dumps are stable enough to diff in the test suite
- codegen.c has no fprintf in the instruction paths; the printer
  lives in its own file (ir.c) and codegen.c shrinks to resolve +
  layout + glue
