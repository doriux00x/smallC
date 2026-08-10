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

static void indent(int d) {
  for (int i = 0; i < d; i++)
    printf("  ");
}

static const char *op_name(int op) {
  switch (op) {
    case OP_SHL: return "<<";
    case OP_SHR: return ">>";
    case OP_EQ:  return "==";
    case OP_NE:  return "!=";
    case OP_LE:  return "<=";
    case OP_GE:  return ">=";
    case OP_LOGAND: return "&&";
    case OP_LOGOR:  return "||";
    case OP_ADD_ASSIGN: return "+=";
    case OP_SUB_ASSIGN: return "-=";
    case OP_MUL_ASSIGN: return "*=";
    case OP_DIV_ASSIGN: return "/=";
    case OP_MOD_ASSIGN: return "%=";
    case OP_SHL_ASSIGN: return "<<=";
    case OP_SHR_ASSIGN: return ">>=";
    case OP_AND_ASSIGN: return "&=";
    case OP_OR_ASSIGN:  return "|=";
    case OP_XOR_ASSIGN: return "^=";
    case OP_INC: return "++";
    case OP_DEC: return "--";
    case '+': return "+";
    case '-': return "-";
    case '*': return "*";
    case '/': return "/";
    case '%': return "%";
    case '&': return "&";
    case '|': return "|";
    case '^': return "^";
    case '~': return "~";
    case '!': return "!";
    case '<': return "<";
    case '>': return ">";
    case '=': return "=";
  }
  return "?";
}

static void dump_expr(Node *n, int d) {
  indent(d);
  switch (n->kind) {
    case ND_NUM:
      printf("num %d\n", n->val);
      return;
    case ND_STR:
      printf("str len=%d \"%s\"\n", n->str_len, n->str);
      return;
    case ND_VAR:
      printf("var %s\n", n->name);
      return;
    case ND_ASSIGN:
      printf("assign %s\n", op_name(n->op));
      dump_expr(n->lhs, d + 1);
      dump_expr(n->rhs, d + 1);
      return;
    case ND_BIN:
      printf("bin %s\n", op_name(n->op));
      dump_expr(n->lhs, d + 1);
      dump_expr(n->rhs, d + 1);
      return;
    case ND_UNARY:
      printf("un %s\n", op_name(n->op));
      dump_expr(n->lhs, d + 1);
      return;
    case ND_COND:
      printf("cond\n");
      dump_expr(n->cond, d + 1);
      dump_expr(n->then, d + 1);
      dump_expr(n->els, d + 1);
      return;
    case ND_CALL:
      printf("call\n");
      dump_expr(n->lhs, d + 1);
      for (Node *a = n->args; a; a = a->next)
        dump_expr(a, d + 1);
      return;
    case ND_INDEX:
      printf("index\n");
      dump_expr(n->lhs, d + 1);
      dump_expr(n->rhs, d + 1);
      return;
    case ND_MEMBER:
      printf("member %s%s\n", n->is_pntr ? "->" : ".", n->name);
      dump_expr(n->lhs, d + 1);
      return;
    case ND_SIZEOF:
      if (n->lhs) {
        printf("sizeof\n");
        dump_expr(n->lhs, d + 1);
      } else {
        printf("sizeof type = %d bytes\n", n->targ->size);
      }
      return;
    default:
      printf("<unknown node %d>\n", n->kind);
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
    if (n->init) {
      printf(" =\n");
      dump_expr(n->init, 2);
    } else {
      printf("\n");
    }
    count++;
  }
  printf("%d declaration(s)\n", count);
  return 0;
}