CC      ?= cc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -Werror -g
OBJDIR   = build
OBJS     = $(OBJDIR)/main.o $(OBJDIR)/util.o $(OBJDIR)/lexer.o $(OBJDIR)/parser.o $(OBJDIR)/ast.o $(OBJDIR)/codegen.o
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
	./$(BIN) -a tests/float.c
	./$(BIN) -t tests/lexer.c
	@set -e; for t in run1 run2 run3 run4 run5 runfptr runstruct runfloat runinit runswitch runtypedef runenum rununion; do \
	  echo "== $$t =="; \
	  ./$(BIN) tests/$$t.c; \
	  $(CC) $(CFLAGS) -o $(OBJDIR)/$$t $(OBJDIR)/$$t.s; \
	  ./$(OBJDIR)/$$t; \
	done

clean:
	rm -rf $(OBJDIR) $(BIN)

# whole compiler has to stay under 2MB, watch this
size: $(BIN)
	@size $(BIN)
	@echo "binary: $$(stat -c%s $(BIN)) bytes"

.PHONY: test clean size
