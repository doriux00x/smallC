CC      ?= cc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -Werror -g
OBJDIR   = build
OBJS     = $(OBJDIR)/main.o $(OBJDIR)/util.o $(OBJDIR)/lexer.o $(OBJDIR)/parser.o $(OBJDIR)/ast.o $(OBJDIR)/codegen.o $(OBJDIR)/preproc.o
BIN      = smallcc

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: src/%.c $(wildcard src/*.h) | $(OBJDIR)
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
	./$(BIN) -a tests/runmulti1.c tests/runmulti2.c
	./$(BIN) -t tests/lexer.c
	@set -e; for t in run1 run2 run3 run4 run5 runfptr runstruct runfloat runinit runlit runswitch runtypedef runenum rununion runconst runstatic rungoto runbool runvolatile runregister runpreproc runstdarg runclit rundesig runbit runcomma runvla runassert runalign rungeneric runstmt runspec runtypeof runattrs runbuiltin runlab runlong runtls; do \
	  echo "== $$t =="; \
	  ./$(BIN) tests/$$t.c; \
	  $(CC) $(CFLAGS) -o $(OBJDIR)/$$t $(OBJDIR)/$$t.s; \
	  ./$(OBJDIR)/$$t; \
	done
	@echo "== multi-file =="; \
	./$(BIN) tests/runmulti1.c tests/runmulti2.c; \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runmulti $(OBJDIR)/runmulti1.s $(OBJDIR)/runmulti2.s; \
	./$(OBJDIR)/runmulti; \
	echo "== rundef =="; \
	./$(BIN) -D FLAG=7 -D OTHER -U __linux__ tests/rundef.c; \
	$(CC) $(CFLAGS) -o $(OBJDIR)/rundef $(OBJDIR)/rundef.s; \
	./$(OBJDIR)/rundef; \
	echo "== rune =="; \
	./$(BIN) -E -D FLAG=9 tests/rune.c > $(OBJDIR)/rune.out && \
	gcc -E -P -D FLAG=9 tests/rune.c > $(OBJDIR)/rune.gcc && \
	diff $(OBJDIR)/rune.out $(OBJDIR)/rune.gcc && \
	./$(BIN) $(OBJDIR)/rune.out && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/rune $(OBJDIR)/rune.s && \
	./$(OBJDIR)/rune && \
	echo "== runwarning ==" && \
	./$(BIN) tests/runwarning.c 2>$(OBJDIR)/warn.out && \
	grep -q '^tests/runwarning.c:6:3: warning: #warning "level two build" \[-Wcpp\]$$' $(OBJDIR)/warn.out && \
	test $$(grep -c 'warning: #warning' $(OBJDIR)/warn.out) -eq 1 && \
	gcc -fsyntax-only tests/runwarning.c 2>&1 | grep -o '#warning.*\[-Wcpp\]' | sort -u > $(OBJDIR)/warn.gcc && \
	grep -o '#warning.*\[-Wcpp\]' $(OBJDIR)/warn.out | sort -u > $(OBJDIR)/warn.ours && \
	diff $(OBJDIR)/warn.ours $(OBJDIR)/warn.gcc && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runwarning $(OBJDIR)/runwarning.s && \
	./$(OBJDIR)/runwarning && \
	echo "== rundaytime ==" && \
	./$(BIN) tests/rundaytime.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/rundaytime $(OBJDIR)/rundaytime.s && \
	./$(OBJDIR)/rundaytime && \
	./$(BIN) -E tests/rundaytime.c > $(OBJDIR)/daytime.out && \
	nowdate="$$(date +'%b %e %Y')" && \
	nowtime="$$(date +'%H:%M:%S')" && \
	grep -q "build_date = \"$$nowdate\";" $(OBJDIR)/daytime.out && \
	grep -q "build_time = \"$$nowtime\";" $(OBJDIR)/daytime.out && \
	echo "== runelifdef ==" && \
	./$(BIN) -E tests/runelifdef.c > $(OBJDIR)/elifdef.out && \
	gcc -E -P tests/runelifdef.c > $(OBJDIR)/elifdef.gcc && \
	diff $(OBJDIR)/elifdef.out $(OBJDIR)/elifdef.gcc && \
	./$(BIN) tests/runelifdef.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runelifdef $(OBJDIR)/runelifdef.s && \
	./$(OBJDIR)/runelifdef && \
	gcc -o $(OBJDIR)/runelifdef.gcc tests/runelifdef.c && \
	./$(OBJDIR)/runelifdef.gcc && \
	echo "== runincline ==" && \
	./$(BIN) -E tests/runincline.c > $(OBJDIR)/incline.out && \
	gcc -E -P tests/runincline.c > $(OBJDIR)/incline.gcc && \
	diff $(OBJDIR)/incline.out $(OBJDIR)/incline.gcc && \
	./$(BIN) tests/runincline.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runincline $(OBJDIR)/runincline.s && \
	./$(OBJDIR)/runincline && \
	gcc -o $(OBJDIR)/runincline.gcc tests/runincline.c && \
	./$(OBJDIR)/runincline.gcc && \
	echo "== runincl ==" && \
	./$(BIN) -E -Itests/inc -include inc1.h tests/runincl.c > $(OBJDIR)/incl.out && \
	gcc -E -P -Itests/inc -include inc1.h tests/runincl.c > $(OBJDIR)/incl.gcc && \
	diff $(OBJDIR)/incl.out $(OBJDIR)/incl.gcc && \
	./$(BIN) -Itests/inc -include inc1.h tests/runincl.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runincl $(OBJDIR)/runincl.s && \
	./$(OBJDIR)/runincl && \
	gcc -Itests/inc -include inc1.h -o $(OBJDIR)/runincl.gcc tests/runincl.c && \
	./$(OBJDIR)/runincl.gcc && \
	echo "== runinclnext ==" && \
	./$(BIN) -E -Itests/inc -Itests/inc2 tests/runinclnext.c > $(OBJDIR)/inclnext.out && \
	gcc -E -P -Itests/inc -Itests/inc2 tests/runinclnext.c > $(OBJDIR)/inclnext.gcc && \
	diff $(OBJDIR)/inclnext.out $(OBJDIR)/inclnext.gcc && \
	./$(BIN) -Itests/inc -Itests/inc2 tests/runinclnext.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runinclnext $(OBJDIR)/runinclnext.s && \
	./$(OBJDIR)/runinclnext && \
	gcc -Itests/inc -Itests/inc2 -o $(OBJDIR)/runinclnext.gcc tests/runinclnext.c && \
	./$(OBJDIR)/runinclnext.gcc && \
	echo "== runpoison ==" && \
	./$(BIN) -Itests/inc tests/runpoison.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runpoison $(OBJDIR)/runpoison.s && \
	./$(OBJDIR)/runpoison && \
	gcc -Itests/inc -o $(OBJDIR)/runpoison.gcc tests/runpoison.c && \
	./$(OBJDIR)/runpoison.gcc && \
	! ./$(BIN) -Itests/inc tests/runpoison_bad.c >/dev/null 2>&1 && \
	! gcc -Itests/inc -o /dev/null tests/runpoison_bad.c >/dev/null 2>&1 && \
	./$(BIN) -Itests/inc tests/runpoison_bad.c 2>&1 | grep -o 'attempt to use poisoned "sneaky"' | sort -u > $(OBJDIR)/poison.ours && \
	gcc -Itests/inc -o /dev/null tests/runpoison_bad.c 2>&1 | grep -o 'attempt to use poisoned "sneaky"' | sort -u > $(OBJDIR)/poison.gcc && \
	diff $(OBJDIR)/poison.ours $(OBJDIR)/poison.gcc && \
	echo "== runinclvl ==" && \
	./$(BIN) -E -Itests/inc tests/runinclvl.c > $(OBJDIR)/inclvl.out && \
	gcc -E -P -Itests/inc tests/runinclvl.c > $(OBJDIR)/inclvl.gcc && \
	diff $(OBJDIR)/inclvl.out $(OBJDIR)/inclvl.gcc && \
	./$(BIN) -Itests/inc tests/runinclvl.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runinclvl $(OBJDIR)/runinclvl.s && \
	./$(OBJDIR)/runinclvl && \
	gcc -Itests/inc -o $(OBJDIR)/runinclvl.gcc tests/runinclvl.c && \
	./$(OBJDIR)/runinclvl.gcc && \
	echo "== runpmsg ==" && \
	./$(BIN) tests/runpmsg.c > $(OBJDIR)/runpmsg.s 2> $(OBJDIR)/pmsg.ours && \
	gcc -fsyntax-only tests/runpmsg.c 2> $(OBJDIR)/pmsg.gcc && \
	test $$(grep -c "note:" $(OBJDIR)/pmsg.ours) -eq 3 && \
	grep "note:" $(OBJDIR)/pmsg.ours | awk -F"message: " '{print $$2}' | tr -d "'" > $(OBJDIR)/pmsg.o1 && \
	grep "note:" $(OBJDIR)/pmsg.gcc | tr -d '\200-\377' | awk -F"message: " '{print $$2}' | tr -d "'" > $(OBJDIR)/pmsg.o2 && \
	diff $(OBJDIR)/pmsg.o1 $(OBJDIR)/pmsg.o2 && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runpmsg $(OBJDIR)/runpmsg.s && \
	./$(OBJDIR)/runpmsg && \
	gcc -o $(OBJDIR)/runpmsg.gcc tests/runpmsg.c && \
	./$(OBJDIR)/runpmsg.gcc
	echo "== runinclnext ==" && \
	./$(BIN) -E -Itests/inc -Itests/inc2 tests/runinclnext.c > $(OBJDIR)/inclnext.out && \
	gcc -E -P -Itests/inc -Itests/inc2 tests/runinclnext.c > $(OBJDIR)/inclnext.gcc && \
	diff $(OBJDIR)/inclnext.out $(OBJDIR)/inclnext.gcc && \
	./$(BIN) -Itests/inc -Itests/inc2 tests/runinclnext.c && \
	$(CC) $(CFLAGS) -o $(OBJDIR)/runinclnext $(OBJDIR)/runinclnext.s && \
	./$(OBJDIR)/runinclnext && \
	gcc -Itests/inc -Itests/inc2 -o $(OBJDIR)/runinclnext.gcc tests/runinclnext.c && \
	./$(OBJDIR)/runinclnext.gcc

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
