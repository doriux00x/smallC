#include "ast.h"
#include "parser.h"
#include "token.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump_type(Type *t) {
  const char *pre = t->is_unsigned ? "unsigned " : "";
  switch (t->kind) {
    case TY_VOID:   printf("void"); return;
    case TY_CHAR:   printf("%schar", pre); return;
    case TY_SHORT:  printf("%sshort", pre); return;
    case TY_INT:    printf("%sint", pre); return;
    case TY_LONG:
      printf("%s%s", pre, t->is_longlong ? "long long" : "long");
      return;
    case TY_FLOAT:  printf("float"); return;
    case TY_DOUBLE: printf("double"); return;
    case TY_PTR:    printf("ptr->"); dump_type(t->base); return;
    case TY_ARRAY:
      printf("array[%d]of ", t->array_len);
      dump_type(t->base);
      return;
  }
}

static void usage(void) {
  fprintf(stderr, "usage: smallcc [-t] <file.c>\n");
  exit(1);
}

int main(int argc, char **argv) {
  int dump_tokens = 0;
  char *path;

  if (argc == 3 && strcmp(argv[1], "-t") == 0) {
    dump_tokens = 1;
    path = argv[2];
  } else if (argc == 2) {
    path = argv[1];
  } else {
    usage();
  }

  g_src = read_file(path);

  Token *toks = tokenize(g_src);

  if (dump_tokens) {
    for (Token *t = toks; t; t = t->next) {
      if (t->kind == TK_EOF) {
        printf("EOF\n");
        break;
      }
      if (t->kind == TK_PUNCT)
        printf("PUNCT '%.*s'\n", t->len, t->loc);
      else if (t->kind == TK_NUM)
        printf("NUM %d\n", t->val);
      else
        printf("%s '%.*s'\n", token_kind_name(t->kind), t->len, t->loc);
    }
    return 0;
  }

  Node *root = parse(toks);

  int count = 0;
  for (Node *n = root; n; n = n->next) {
    printf("%-16s %2d bytes : ", n->name, n->type->size);
    dump_type(n->type);
    printf("\n");
    count++;
  }
  printf("%d declaration(s)\n", count);
  return 0;
}
