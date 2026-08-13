#include "ast.h"
#include "codegen.h"
#include "parser.h"
#include "preproc.h"
#include "token.h"
#include "util.h"

#include "libc.h"

static void dump_type(Type *t) {
  static Type *dumping;   /* chain of types being printed, to break
                           * self-referential structs */
  const char *pre = t->is_unsigned ? "unsigned " : "";
  const char *pre2 = t->is_const && t->is_volatile ? "const volatile " :
                     t->is_const ? "const " :
                     t->is_volatile ? "volatile " : "";
  switch (t->kind) {
    case TY_VOID:   printf("%svoid", pre2); return;
    case TY_CHAR:
      if (t->is_bool)
        printf("%s_Bool", pre2);
      else
        printf("%s%schar", pre2, pre);
      return;
    case TY_SHORT:  printf("%s%sshort", pre2, pre); return;
    case TY_INT:    printf("%s%sint", pre2, pre); return;
    case TY_LONG:
      printf("%s%s%s", pre2, pre, t->is_longlong ? "long long" : "long");
      return;
    case TY_FLOAT:  printf("%sfloat", pre2); return;
    case TY_DOUBLE: printf("%sdouble", pre2); return;
    case TY_PTR:    printf("ptr->"); dump_type(t->base); return;
    case TY_ARRAY:
      printf("array[%d]of ", t->array_len);
      dump_type(t->base);
      return;
    case TY_FUNC:
      printf("func(");
      for (Node *p = t->params; p; p = p->next) {
        dump_type(p->type);
        if (p->name)
          printf(" %s", p->name);
        if (p->next)
          printf(", ");
      }
      printf(") -> ");
      dump_type(t->ret);
      return;
    case TY_STRUCT:
    case TY_UNION: {
      for (Type *t2 = dumping; t2; t2 = t2->mark_prev)
        if (t2 == t) {
          printf("%s(...)", t->kind == TY_STRUCT ? "struct" : "union");
          return;
        }
      printf("%s(align %d, size %d){", t->kind == TY_STRUCT ? "struct" : "union",
             t->align, t->size);
      t->mark_prev = dumping;
      dumping = t;
      for (Member *m = t->members; m; m = m->next) {
        printf("%s@%d: ", m->name, m->offset);
        dump_type(m->type);
        if (m->next)
          printf(", ");
      }
      dumping = t->mark_prev;
      printf("}");
      return;
    }
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
      if (n->is_float)
        printf("num %f\n", n->fval);
      else
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
    case ND_CAST:
      printf("cast to ");
      dump_type(n->targ);
      printf("\n");
      dump_expr(n->lhs, d + 1);
      return;
    case ND_INIT_LIST:
      printf("init list\n");
      for (Node *e = n->elems; e; e = e->next)
        dump_expr(e, d + 1);
      return;
    case ND_COMP_LIT:
      printf("compound literal of ");
      dump_type(n->targ);
      printf("\n");
      for (Node *e = n->elems; e; e = e->next)
        dump_expr(e, d + 1);
      return;
    default:
      printf("<unknown node %d>\n", n->kind);
  }
}

static void dump_stmt(Node *n, int d);
static void dump_stmt_chain(Node *s, int d) {
  for (; s; s = s->next)
    dump_stmt(s, d);
}

static void dump_stmt(Node *n, int d) {
  indent(d);
  switch (n->kind) {
    case ND_BLOCK:
      printf("block\n");
      dump_stmt_chain(n->body, d + 1);
      return;
    case ND_DECL:
      printf("decl %-14s %2d bytes : ", n->name, n->type->size);
      if (n->is_static)
        printf("static ");
      else if (n->is_extern)
        printf("extern ");
      dump_type(n->type);
      printf("\n");
      if (n->init)
        dump_expr(n->init, d + 1);
      return;
    case ND_EXPR_STMT:
      printf("expr stmt\n");
      if (n->lhs)
        dump_expr(n->lhs, d + 1);
      return;
    case ND_IF:
      printf("if\n");
      dump_expr(n->cond, d + 1);
      dump_stmt(n->then, d + 1);
      if (n->els)
        dump_stmt(n->els, d + 1);
      return;
    case ND_WHILE:
      printf("while\n");
      dump_expr(n->cond, d + 1);
      dump_stmt(n->then, d + 1);
      return;
    case ND_DO_WHILE:
      printf("do-while\n");
      dump_stmt(n->then, d + 1);
      dump_expr(n->cond, d + 1);
      return;
    case ND_FOR:
      printf("for\n");
      if (n->init)
        dump_stmt_chain(n->init, d + 1);
      if (n->cond)
        dump_expr(n->cond, d + 1);
      if (n->inc)
        dump_expr(n->inc, d + 1);
      dump_stmt(n->then, d + 1);
      return;
    case ND_SWITCH:
      printf("switch\n");
      dump_expr(n->cond, d + 1);
      dump_stmt(n->body, d + 1);
      return;
    case ND_CASE:
      if (n->lhs) {
        printf("case:\n");
        dump_expr(n->lhs, d + 1);
      } else {
        printf("default\n");
      }
      dump_stmt(n->body, d + 1);
      return;
    case ND_LABEL:
      printf("label %s\n", n->name);
      dump_stmt(n->body, d + 1);
      return;
    case ND_GOTO:
      printf("goto %s\n", n->name);
      return;
    case ND_RETURN:
      printf("return\n");
      if (n->lhs)
        dump_expr(n->lhs, d + 1);
      return;
    case ND_BREAK:
      printf("break\n");
      return;
    case ND_CONTINUE:
      printf("continue\n");
      return;
    case ND_FUNC:
      printf("func %s : ", n->name);
      if (n->is_static)
        printf("static ");
      else if (n->is_extern)
        printf("extern ");
      dump_type(n->type);
      printf("\n");
      if (n->body)
        dump_stmt(n->body, d + 1);
      else {
        indent(d + 1);
        printf("(prototype)\n");
      }
      return;
    default:
      dump_expr(n, d);
  }
}

static void usage(void) {
  fprintf(stderr, "usage: smallcc [-a|-t] <file.c>\n");
  fprintf(stderr, "       smallcc [-I dir]... <file.c>...   each compiles to build/<base>.s\n");
  exit(1);
}

/* strip the directory and extension, so tests/run1.c is run1.s */
static char *out_base(char *path) {
  char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  char *dot = strrchr(base, '.');
  if (dot)
    *dot = '\0';
  char *out = xmalloc(strlen(base) + 8);
  sprintf(out, "build/%s.s", base);
  return out;
}

/* one translation unit, end to end. every stage resets its own
 * state (resolve rewinds the scope and label counter, codegen too),
 * so several files in one run are exactly several separate runs */
static void compile_file(char *path) {
  g_src = read_file(path);
  Token *toks = preprocess(tokenize(g_src), path);
  Node *root = parse(toks);
  resolve(root);
  char *outpath = out_base(path);
  mkdir("build", 0755);   /* cc(1) would just fail, we're kinder */
  codegen(root, outpath);
}

int main(int argc, char **argv) {
  enum { MODE_COMPILE, MODE_DUMP_AST, MODE_DUMP_TOKENS } mode = MODE_COMPILE;
  char *path;

  if (argc == 3 && strcmp(argv[1], "-a") == 0) {
    mode = MODE_DUMP_AST;
    path = argv[2];
  } else if (argc == 3 && strcmp(argv[1], "-t") == 0) {
    mode = MODE_DUMP_TOKENS;
    path = argv[2];
  } else if (argc >= 2) {
    path = NULL;
  } else {
    usage();
  }

  if (mode == MODE_COMPILE) {
    for (int i = 1; i < argc; i++) {
      if (strncmp(argv[i], "-I", 2) == 0) {
        char *dir = argv[i] + 2;
        if (!*dir && i + 1 < argc)
          dir = argv[++i];
        add_include_dir(dir);
      } else {
        compile_file(argv[i]);
      }
    }
    return 0;
  }

  g_src = read_file(path);
  Token *toks = preprocess(tokenize(g_src), path);

  if (mode == MODE_DUMP_TOKENS) {
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

  if (mode == MODE_DUMP_AST) {

  int count = 0;
  for (Node *n = root; n; n = n->next) {
    if (n->kind == ND_FUNC) {
      dump_stmt(n, 0);
      count++;
      continue;
    }
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
  printf("%d top-level item(s)\n", count);
    return 0;
  }
}