CC      ?= cc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -Werror -g
OBJDIR   = build
OBJS     = $(OBJDIR)/main.o $(OBJDIR)/util.o $(OBJDIR)/lexer.o $(OBJDIR)/parser.o $(OBJDIR)/ast.o $(OBJDIR)/codegen.o $(OBJDIR)/preproc.o
BIN      = smallcc

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

# dump mode only validates the front end, the run tests compile the
# generated assembly with cc(1) and execute the result
test: $(BIN)
	./$(BIN) -a tests/decls.c
	./$(BIN) -a tests/expr.c
	./$(BIN) -a tests/func.c
	./$(BIN) -a tests/funcptr.c
	./$(BIN) -a tests/struct.c
	./$(BIN) -a tests/switch.c
	./$(BIN) -a tests/typedef.c
	./$(BIN) -a tests/enum.c
	./$(BIN) -a tests/union.c
	./$(BIN) -a tests/const.c
	./$(BIN) -a tests/static.c
	./$(BIN) -a tests/goto.c
	./$(BIN) -a tests/bool.c
	./$(BIN) -a tests/volatile.c
	./$(BIN) -a tests/register.c
	./$(BIN) -a tests/float.c
	./$(BIN) -t tests/lexer.c
	@set -e; for t in run1 run2 run3 run4 run5 runfptr runstruct runfloat runinit runswitch runtypedef runenum rununion runconst runstatic rungoto runbool runvolatile runregister; do \
	  echo "== $$t =="; \
	  ./$(BIN) tests/$$t.c; \
	  $(CC) $(CFLAGS) -o $(OBJDIR)/$$t $(OBJDIR)/$$t.s; \
	  ./$(OBJDIR)/$$t; \
	done
	@echo "== multi-file =="; \
	./$(BIN) tests/runmulti1.c tests/runmulti2.c; \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runmulti $(OBJDIR)/runmulti1.s $(OBJDIR)/runmulti2.s; \
	./$(OBJDIR)/runmulti

clean:
	rm -rf $(OBJDIR) $(BIN) build2 build3 smallcc2 smallcc3

# self-hosting: the compiler generates its own assembly; the six
# self-compiled units are assembled with cc(1) and linked against the
# host-built util.o (the only unit that still needs va_list)
SELF2  = build2
SELF3  = build3
SELF   = main lexer parser ast codegen preproc

$(SELF2)/main.o: $(BIN)
	@mkdir -p $(SELF2) $(SELF3)
	@for f in $(SELF); do \
	  ./$(BIN) src/$$f.c || exit 1; \
	  $(CC) -c build/$$f.s -o $(SELF2)/$$f.o || exit 1; \
	done

smallcc2: $(SELF2)/main.o
	$(CC) $(CFLAGS) -o $@ build/util.o $(SELF2)/main.o $(SELF2)/lexer.o $(SELF2)/parser.o $(SELF2)/ast.o $(SELF2)/codegen.o $(SELF2)/preproc.o

smallcc3: smallcc2
	@for f in $(SELF); do \
	  ./smallcc2 src/$$f.c || exit 1; \
	  $(CC) -c build/$$f.s -o $(SELF3)/$$f.o || exit 1; \
	done
	$(CC) $(CFLAGS) -o $@ build/util.o $(SELF3)/main.o $(SELF3)/lexer.o $(SELF3)/parser.o $(SELF3)/ast.o $(SELF3)/codegen.o $(SELF3)/preproc.o

selftest: smallcc3
	cmp smallcc2 smallcc3
	$(MAKE) test BIN=smallcc2

.PHONY: test clean size selftest
