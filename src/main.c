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
        if (m->name)
          printf("%s@%d", m->name, m->offset);
        else
          printf(":%d", m->bit_width);
        if (m->is_bitfield)
          printf(".%d-%d", m->bit_offset, m->bit_offset + m->bit_width);
        printf(": ");
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
    case ',': return ",";
  }
  return "?";
}

static void dump_stmt(Node *n, int d);
static void dump_stmt_chain(Node *s, int d);

static void dump_expr(Node *n, int d) {
  indent(d);
  switch (n->kind) {
    case ND_NUM:
      if (n->is_float)
        printf("num %f\n", n->fval);
      else
        printf("num %ld\n", n->val);
      return;
    case ND_STR:
      printf("str len=%d \"%s\"\n", n->str_len, n->str);
      return;
    case ND_LABEL_ADDR:
      printf("&&label %s\n", n->name);
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
    case ND_STMT_EXPR:
      printf("statement expression\n");
      dump_stmt_chain(n->body, d + 1);
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
    case ND_ALIGNOF:
      if (n->lhs) {
        printf("alignof\n");
        dump_expr(n->lhs, d + 1);
      } else {
        printf("alignof type = %d bytes\n", n->targ->align);
      }
      return;
    case ND_GENERIC:
      printf("generic\n");
      dump_expr(n->cond, d + 1);
      for (Node *a = n->els; a; a = a->next) {
        if (a->targ)
          printf("assoc type\n");
        else
          printf("assoc default\n");
        dump_expr(a->lhs, d + 1);
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
    case ND_DESIG:
      if (n->lhs) {
        printf("[");
        dump_expr(n->lhs, 0);
        printf("] = designator\n");
      } else {
        printf(".%s = designator\n", n->name);
      }
      dump_expr(n->then, d + 1);
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
      if (n->is_thread)
        printf("thread ");
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
        if (n->rhs) {
          printf("...to:\n");
          dump_expr(n->rhs, d + 1);
        }
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
    case ND_GOTO_PTR:
      printf("goto *\n");
      dump_expr(n->lhs, d + 1);
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
      if (n->is_inline)
        printf("inline ");
      if (n->is_noreturn)
        printf("_Noreturn ");
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

/* -M side flags: -MP appends a phony rule per dependency (so a
 * deleted header does not break `make` mid-build), and -MD/-MMD
 * write the rule to build/<base>.d as a side effect of compiling
 * instead of taking the whole run over */
static int g_dep_phony;
static int g_deps_sidecar;

/* -E -P: gcc's blank-collapsed form. plain -E (the default) emits
 * the linemarkers instead */
static int g_flag_P;

static void write_rule(char *path, FILE *out);

static void usage(void) {
  fprintf(stderr, "usage: smallcc [-a|-t|-E|-M] <file.c>\n");
  fprintf(stderr, "       smallcc [-I dir]... [-D NAME[=VALUE]]... [-U NAME]... <file.c>...\n");
  fprintf(stderr, "       -E preprocesses to stdout (gcc's default form:\n");
  fprintf(stderr, "         linemarkers; add -P for the -E -P form: no #\n");
  fprintf(stderr, "         markers, blank lines collapse); -M writes the make\n");
  fprintf(stderr, "         dependency rule (-MP phony rules; -MD/-MMD write\n");
  fprintf(stderr, "         build/<base>.d); each other file compiles to\n");
  fprintf(stderr, "         build/<base>.s\n");
  exit(1);
}

/* strip the directory and extension, so tests/run1.c is run1.s */
static char *out_base(char *path) {
  char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  char *dot = strrchr(base, '.');
  /* build/<stem>.s without touching the caller's path */
  int stem = dot ? (int)(dot - base) : (int)strlen(base);
  char *out = xmalloc(stem + 9);
  memcpy(out, "build/", 6);
  memcpy(out + 6, base, stem);
  memcpy(out + 6 + stem, ".s", 3);
  return out;
}

/* every compiled or preprocessed file resets its own state, so
 * several files in one run are exactly several separate runs */
static void compile_file(char *path) {
  reset_deps();
  g_src = read_file(path);
  Token *toks = preprocess(tokenize(g_src), path);
  Node *root = parse(toks);
  resolve(root);
  char *outpath = out_base(path);
  mkdir("build", 0755);   /* cc(1) would just fail, we're kinder */
  codegen(root, outpath);
  if (g_deps_sidecar) {
    /* -MD/-MMD: the rule lands next to the assembly, target named
     * after the .o this run would feed */
    char *d = out_base(path);
    d[strlen(d) - 1] = 'd';
    FILE *f = fopen(d, "w");
    if (!f)
      error("cannot open %s", d);
    write_rule(path, f);
    fclose(f);
  }
}

/* -E: the preprocessed chain back out as text. synthesized tokens
 * (builtin macro values, stringized arguments) print from their
 * synth field; everything else prints its source bytes, so the
 * output round-trips through the compiler as a valid translation
 * unit. line structure follows gcc -E -P: each line keeps its
 * leading spaces (a tab line-start prints one space), and blank
 * lines collapse */
static void print_preprocessed(Token *t) {
  int first = 1;
  int just_nl = 0;
  for (; t->kind != TK_EOF; t = t->next) {
    if (t->at_bol) {
      if (!first && !just_nl)
        printf("\n");
      if (t->indent < 0)
        printf(" ");
      else
        for (int i = 0; i < t->indent; i++)
          printf(" ");
      just_nl = 1;
    } else if (!first && t->space) {
      printf(" ");
    }
    if (t->synth)
      printf("%s", t->synth);
    else
      printf("%.*s", t->len, t->loc);
    first = 0;
    just_nl = 0;
  }
  printf("\n");
}

/* leading whitespace when a token opens an output row: its source
 * column (gcc continues a consumed line at the byte offset where
 * output resumes), or the inherited indent for synthesized tokens */
static void print_row_indent(Token *t) {
  int n = (t->synth || t->virt) ? (t->indent < 0 ? 1 : t->indent)
                                : col_at(t->loc) - 1;
  for (int i = 0; i < n; i++)
    printf(" ");
}

static void print_lm(Token *t, char *path);

static void preproc_file(char *path) {
  g_src = read_file(path);
  lm_set(!g_flag_P);
  Token *toks = preprocess(tokenize(g_src), path);
  if (g_flag_P)
    print_preprocessed(toks);
  else
    print_lm(toks, path);
}

/* plain -E, gcc's default form: the "# line "file"" markers around
 * the token stream. the opening triple mirrors gcc's (# 0 for the
 * primary file, <built-in> and <command-line>), then each file
 * transition recorded by the preprocessor prints its marker - " 1"
 * entering an include, " 2" returning past its directive, none on a
 * plain start or resync. between rows the source's own line
 * structure shows through: up to seven skipped lines pad as blank
 * rows, a longer jump resyncs with a bare marker at the new line.
 * row breaks follow at_bol like the -P printer does, so tokens a
 * macro expansion splices into one line stay on one line */
static void print_lm(Token *t, char *path) {
  printf("# 0 \"%s\"\n", path);
  printf("# 0 \"<built-in>\"\n");
  printf("# 0 \"<command-line>\"\n");
  int written = 0;      /* last source row accounted for */
  int row_open = 0;
  const char *cur_name = path;
  for (; t->kind != TK_EOF; t = t->next) {
    if (t->kind == TK_LMARK) {
      if (t->val == 1 && t->ev_line - 1 > written) {
        /* lines of the parent before the #include directive are
         * still unrendered - directive-only and blank source lines
         * show as blank rows here, or resync when too far back */
        int skipped = t->ev_line - written - 1;
        if (row_open) {
          printf("\n");
          row_open = 0;
        }
        if (skipped > 7)
          printf("# %d \"%s\"\n", t->ev_line, cur_name);
        else
          for (int i = 0; i < skipped; i++)
            printf("\n");
      } else if (row_open) {
        printf("\n");
        row_open = 0;
      }
      if (t->val == 0)
        printf("# 1 \"%s\"\n", t->name);
      else
        printf("# %d \"%s\"%s\n", t->line, t->name,
               t->val == 1 ? " 1" : " 2");
      written = t->line - 1;
      cur_name = t->name;
      continue;
    }
    if (!t->exp && t->line > written) {
      /* a real token from a fresh source line opens that row:
       * blank runs up to seven lines pad as newlines, a longer
       * jump resyncs with a bare marker; expansion-carried tokens
       * glue onto whatever row is open instead */
      int skipped = t->line - written - 1;
      if (skipped > 7) {
        if (row_open)
          printf("\n");
        printf("# %d \"%s\"\n", t->line, cur_name);
        print_row_indent(t);
      } else {
        if (row_open)
          printf("\n");
        for (int i = 0; i < skipped; i++)
          printf("\n");
        print_row_indent(t);
      }
      row_open = 0;
      written = t->line;
    } else if (!row_open) {
      print_row_indent(t);
    } else if (t->space) {
      printf(" ");
    }
    if (t->synth)
      printf("%s", t->synth);
    else
      printf("%.*s", t->len, t->loc);
    row_open = 1;
  }
  if (row_open)
    printf("\n");
}

/* -M: the make dependency rule for one file, gcc's format. the
 * target is the source with .o swapped in; the source leads the
 * dependency list, then every file preprocessed for it in open
 * order, deduped, wrapped with " \" continuations so no line runs
 * past column 73 before a continuation */
static void deps_file(char *path) {
  g_src = read_file(path);
  reset_deps();
  preprocess(tokenize(g_src), path);
  write_rule(path, stdout);
}

/* the -M rule for one file: target = source with .o swapped in, the
 * source leads, then every preprocessed file in open order deduped,
 * wrapped with " \" continuations so no line runs past column 73
 * before a continuation */
static void write_rule(char *path, FILE *out) {
  char *slash = strrchr(path, '/');
  char *base = slash ? slash + 1 : path;
  char *dot = strrchr(base, '.');
  int stem = dot ? (int)(dot - base) : (int)strlen(base);
  char *target = xmalloc(stem + 4);
  memcpy(target, base, stem);
  memcpy(target + stem, ".o", 3);
  int col = fprintf(out, "%s:", target);
  fprintf(out, " ");
  col++;
  col += fprintf(out, "%s", path);
  int ndeps;
  char **deps = get_deps(&ndeps);
  for (int i = 0; i < ndeps; i++) {
    int len = (int)strlen(deps[i]);
    if (col && col + 1 + len > 73) {
      fprintf(out, " \\\n");
      col = 0;
    }
    fprintf(out, " ");
    col++;
    col += fprintf(out, "%s", deps[i]);
  }
  fprintf(out, "\n");
  if (g_dep_phony)
    for (int i = 0; i < ndeps; i++)
      fprintf(out, "%s:\n", deps[i]);
}

/* one -I/-D/-U/-include option, shared by the compile and preprocess
 * modes; the -D NAME[=VALUE], -U NAME and -include FILE forms match
 * the compile loop's two-token-or-fused parsing. returns 1 when
 * argv[*i] was an option (having consumed argv[++*i] for the
 * two-token form, or argv[*i] alone for the fused one) */
static int take_cli_option(int argc, char **argv, int *i) {
  char *a = argv[*i];
  if (strcmp(a, "-P") == 0) {
    /* with -E: gcc's blank-collapsed form instead of linemarkers */
    g_flag_P = 1;
    return 1;
  }
  if (strcmp(a, "-MP") == 0) {
    g_dep_phony = 1;
    return 1;
  }
  if (strcmp(a, "-MD") == 0 || strcmp(a, "-MMD") == 0) {
    /* -MMD would skip system headers; none exist here */
    g_deps_sidecar = 1;
    return 1;
  }
  if (strncmp(a, "-I", 2) == 0) {
    char *dir = a + 2;
    if (!*dir && *i + 1 < argc)
      dir = argv[++*i];
    add_include_dir(dir);
    return 1;
  }
  if (strncmp(a, "-D", 2) == 0 && (a[2] || *i + 1 < argc)) {
    char *def = a[2] ? a + 2 : argv[++*i];
    define_macro_cli(def);
    return 1;
  }
  if (strncmp(a, "-U", 2) == 0 && (a[2] || *i + 1 < argc)) {
    char *name = a[2] ? a + 2 : argv[++*i];
    undef_macro_cli(name);
    return 1;
  }
  if (strncmp(a, "-include", 8) == 0 && (a[8] || *i + 1 < argc)) {
    char *name = a[8] ? a + 8 : argv[++*i];
    add_cli_include(name);
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  enum { MODE_COMPILE, MODE_DUMP_AST, MODE_DUMP_TOKENS, MODE_PREPROC,
         MODE_DEPS } mode
    = MODE_COMPILE;
  char *path;

  if (argc >= 3 && strcmp(argv[1], "-a") == 0) {
    mode = MODE_DUMP_AST;
    path = argv[2];
  } else if (argc >= 3 && strcmp(argv[1], "-t") == 0) {
    mode = MODE_DUMP_TOKENS;
    path = argv[2];
  } else if (argc >= 2 && strcmp(argv[1], "-E") == 0) {
    mode = MODE_PREPROC;
    path = NULL;
  } else if (argc >= 2 && (strcmp(argv[1], "-M") == 0 ||
                           strcmp(argv[1], "-MM") == 0)) {
    /* -MM is the same rule here: every header arrives through -I or
     * the source tree, so nothing counts as a system header */
    mode = MODE_DEPS;
    path = NULL;
  } else if (argc >= 2) {
    path = NULL;
  } else {
    usage();
  }


  if (mode == MODE_COMPILE) {
    for (int i = 1; i < argc; i++) {
      if (take_cli_option(argc, argv, &i))
        continue;
      compile_file(argv[i]);
    }
    return 0;
  }

  if (mode == MODE_DEPS) {
    int nfiles = 0;
    for (int i = 1; i < argc; i++) {
      if (take_cli_option(argc, argv, &i))
        continue;
      if (strncmp(argv[i], "-M", 2) == 0)
        continue;
      deps_file(argv[i]);
      nfiles++;
    }
    if (!nfiles)
      usage();
    return 0;
  }

  if (mode == MODE_PREPROC) {
    int nfiles = 0;
    for (int i = 1; i < argc; i++) {
      if (strncmp(argv[i], "-E", 2) == 0)
        continue;
      if (take_cli_option(argc, argv, &i))
        continue;
      preproc_file(argv[i]);
      nfiles++;
    }
    if (!nfiles)
      usage();
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
        printf("NUM %ld%s\n", t->val, t->is_float ? " (float)" : "");
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
    if (n->is_thread)
      printf("thread ");
    if (n->is_static)
      printf("static ");
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