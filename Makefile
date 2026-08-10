CC      ?= cc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -Werror -g
OBJDIR   = build
OBJS     = $(OBJDIR)/main.o $(OBJDIR)/util.o $(OBJDIR)/lexer.o $(OBJDIR)/parser.o $(OBJDIR)/ast.o
BIN      = smallcc

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

test: $(BIN)
	./$(BIN) tests/decls.c
	./$(BIN) tests/expr.c
	./$(BIN) tests/func.c
	./$(BIN) -t tests/lexer.c

clean:
	rm -rf $(OBJDIR) $(BIN)

# whole compiler has to stay under 2MB, watch this
size: $(BIN)
	@size $(BIN)
	@echo "binary: $$(stat -c%s $(BIN)) bytes"

.PHONY: test clean size
