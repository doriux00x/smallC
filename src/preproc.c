#include "preproc.h"
#include "token.h"
#include "util.h"

#include "libc.h"

/* a bare-bones preprocessor: #define (object- and function-like),
 * #undef, #include ("..." resolves against the including file's
 * directory and the -I dirs, "<...>" against the -I dirs only),
 * #ifdef/#ifndef/#if (constant integer expressions, the defined
 * operator, and constants that are single-token macros), #elif,
 * #else, #endif, and the dynamic macros __LINE__, __FILE__,
 * __COUNTER__, __STDC__, __STDC_VERSION__, __x86_64__, __linux__.
 *
 * stringize (#) and token paste (##) are not supported, nor are
 * variadic macros; both are rejected at definition time.
 *
 * it works on the token stream the lexer produces and splices the
 * expanded tokens back into one flat chain for the parser. macro
 * bodies are token slices of the #define line, bounded by the next
 * at_bol token; anything that makes it into the output is consumed
 * exactly once, so chains may be NULL-terminated in place. */
#define MAX_INCLUDE_DEPTH 64
#define MAX_COND_DEPTH 64
#define MAX_MACRO_ARGS 64
#define MAX_PAINT 128

/* -------- include search path -------- */

static char **incdirs;
static int incdir_n;

void add_include_dir(char *dir) {
  incdirs = xrealloc(incdirs, sizeof(char *) * (incdir_n + 1));
  incdirs[incdir_n++] = dir;
}

/* -------- macros -------- */

typedef struct Macro Macro;
struct Macro {
  Macro *next;
  char *name;
  int is_func;          /* function-like: invoked only as F(...) */
  int nparams;
  char **params;
  Token *body;          /* body tokens, iterated by bounds */
  Token *body_end;
};

static Macro *macros;

static Macro *find_macro(char *name) {
  for (Macro *m = macros; m; m = m->next)
    if (strcmp(m->name, name) == 0)
      return m;
  return NULL;
}

/* blue paint: macros being expanded right now; self-references and
 * cycles just stop here instead of recursing forever */
static Macro *painting[MAX_PAINT];
static int paint_n;

static int is_painted(Macro *m) {
  for (int i = 0; i < paint_n; i++)
    if (painting[i] == m)
      return 1;
  return 0;
}

/* -------- conditionals -------- */

typedef struct {
  int active;         /* is THIS branch kept */
  int ever_on;        /* did any branch get taken yet (#elif/#else) */
  int parent_active;  /* was the enclosing level active */
  int seen_else;      /* an #else has already appeared */
} Cond;

static Cond conds[MAX_COND_DEPTH];
static int cond_n;

static int active_now(void) {
  return cond_n ? conds[cond_n - 1].active : 1;
}

/* -------- token plumbing -------- */

typedef struct {
  Token *head;
  Token **link;
} Chain;

static void chain_init(Chain *c) {
  c->head = NULL;
  c->link = &c->head;
}

static void chain_add(Chain *c, Token *t) {
  *c->link = t;
  c->link = &t->next;
}

static void chain_end(Chain *c) {
  *c->link = NULL;
}

/* a shallow token chain copy; arguments can be substituted into a
 * macro body more than once, and the originals are part of the
 * consumed stream, so every splice gets a fresh chain */
static Token *copy_chain(Token *t) {
  Token head = {0};
  Token *cur = &head;
  for (; t; t = t->next) {
    Token *c = xmalloc(sizeof(Token));
    *c = *t;
    cur = cur->next = c;
  }
  return head.next;
}

/* advance to the next token that starts a line (or EOF): directives
 * never span lines */
static Token *skip_line(Token **pp) {
  Token *t = *pp;
  while (t->kind != TK_EOF && !t->at_bol)
    t = t->next;
  *pp = t;
  return t;
}

/* -------- dynamic macros -------- */

static char *cur_file;
static int counter;

static Token *builtin_macro(Token *t) {
  Token *n = NULL;
  if (strcmp(t->name, "__LINE__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_NUM;
    n->val = t->line;
    return n;
  }
  if (strcmp(t->name, "__FILE__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_STR;
    n->str = xstrdup(cur_file);
    n->str_len = strlen(n->str);
    return n;
  }
  if (strcmp(t->name, "__COUNTER__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_NUM;
    n->val = counter++;
    return n;
  }
  if (strcmp(t->name, "__STDC__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_NUM;
    n->val = 1;
    return n;
  }
  if (strcmp(t->name, "__STDC_VERSION__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_NUM;
    n->val = 199901;
    return n;
  }
  if (strcmp(t->name, "__x86_64__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_NUM;
    n->val = 1;
    return n;
  }
  if (strcmp(t->name, "__linux__") == 0) {
    n = xmalloc(sizeof(Token));
    *n = *t;
    n->kind = TK_NUM;
    n->val = 1;
    return n;
  }
  return NULL;
}

/* the dynamic macros count as defined for #ifdef/#ifndef/defined(),
 * even though they live outside the macro table */
static int builtin_name(char *s) {
  return strcmp(s, "__LINE__") == 0 || strcmp(s, "__FILE__") == 0 ||
         strcmp(s, "__COUNTER__") == 0 || strcmp(s, "__STDC__") == 0 ||
         strcmp(s, "__STDC_VERSION__") == 0 || strcmp(s, "__x86_64__") == 0 ||
         strcmp(s, "__linux__") == 0;
}

/* -------- #if expression evaluator --------
 * constants only: integer literals, defined(X), arithmetic and
 * comparison operators, and parentheses. identifiers that are
 * object-like macros with a single integer token (or a one-hop
 * alias chain to one) use that value; anything else is 0, as C99
 * dictates for undefined identifiers in #if. */

static long eval_lor(Token **pp);

static int is_punct(Token *t, char c) {
  return t->kind == TK_PUNCT && t->len == 1 && t->loc[0] == c;
}

static int is_punct2(Token *t, char *s) {
  return t->kind == TK_PUNCT && t->len == 2 &&
         memcmp(t->loc, s, 2) == 0;
}

static long eval_primary(Token **pp) {
  Token *t = *pp;
  if (t->kind == TK_NUM) {
    if (t->is_float)
      error_at(t->loc, "floating constant in #if");
    *pp = t->next;
    return t->val;
  }
  if (is_punct(t, '(')) {
    *pp = t->next;
    long v = eval_lor(pp);
    if (!is_punct(*pp, ')'))
      error_at((*pp)->loc, "expected ')' in #if");
    *pp = (*pp)->next;
    return v;
  }
  if (t->kind == TK_IDENT) {
    if (strcmp(t->name, "defined") == 0) {
      Token *n = t->next;
      int paren = is_punct(n, '(');
      if (paren)
        n = n->next;
      if (n->kind != TK_IDENT)
        error_at(n->loc, "expected macro name after defined");
      long v = (find_macro(n->name) != NULL) || builtin_name(n->name);
      n = n->next;
      if (paren && !is_punct(n, ')'))
        error_at(n->loc, "expected ')' after defined");
      *pp = n->next;
      return v;
    }
    /* a single-token integer macro (or a one-hop alias to one) */
    Macro *m = find_macro(t->name);
    Token *only = (m && !m->is_func && m->body != m->body_end &&
                   m->body->next == m->body_end) ? m->body : NULL;
    if (only && only->kind == TK_NUM && !only->is_float) {
      *pp = t->next;
      return only->val;
    }
    if (only && only->kind == TK_IDENT) {
      Macro *m2 = find_macro(only->name);
      if (m2 && !m2->is_func && m2->body != m2->body_end &&
          m2->body->next == m2->body_end && m2->body->kind == TK_NUM &&
          !m2->body->is_float) {
        *pp = t->next;
        return m2->body->val;
      }
    }
    /* a builtin macro's value */
    if (builtin_name(t->name)) {
      long bv = 0;
      if (strcmp(t->name, "__STDC__") == 0)
        bv = 1;
      else if (strcmp(t->name, "__STDC_VERSION__") == 0)
        bv = 199901;
      else if (strcmp(t->name, "__LINE__") == 0)
        bv = t->line;
      else if (strcmp(t->name, "__COUNTER__") == 0)
        bv = counter++;
      *pp = t->next;
      return bv;
    }
    *pp = t->next;
    return 0;
  }
  error_at(t->loc, "expected expression in #if");
  return 0;
}

static long eval_unary(Token **pp) {
  Token *t = *pp;
  if (is_punct(t, '!'))  { *pp = t->next; return !eval_unary(pp); }
  if (is_punct(t, '~'))  { *pp = t->next; return ~eval_unary(pp); }
  if (is_punct(t, '-'))  { *pp = t->next; return -eval_unary(pp); }
  if (is_punct(t, '+'))  { *pp = t->next; return eval_unary(pp); }
  return eval_primary(pp);
}

static long eval_mul(Token **pp) {
  long v = eval_unary(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct(t, '*'))       { *pp = t->next; v = v * eval_unary(pp); }
    else if (is_punct(t, '/'))  { *pp = t->next; v = v / eval_unary(pp); }
    else if (is_punct(t, '%'))  { *pp = t->next; v = v % eval_unary(pp); }
    else return v;
  }
}

static long eval_add(Token **pp) {
  long v = eval_mul(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct(t, '+'))       { *pp = t->next; v = v + eval_mul(pp); }
    else if (is_punct(t, '-'))  { *pp = t->next; v = v - eval_mul(pp); }
    else return v;
  }
}

static long eval_shift(Token **pp) {
  long v = eval_add(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct2(t, "<<"))     { *pp = t->next; v = v << eval_add(pp); }
    else if (is_punct2(t, ">>")){ *pp = t->next; v = v >> eval_add(pp); }
    else return v;
  }
}

static long eval_rel(Token **pp) {
  long v = eval_shift(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct(t, '<'))       { *pp = t->next; v = v < eval_shift(pp); }
    else if (is_punct(t, '>'))  { *pp = t->next; v = v > eval_shift(pp); }
    else if (is_punct2(t, "<=")){ *pp = t->next; v = v <= eval_shift(pp); }
    else if (is_punct2(t, ">=")){ *pp = t->next; v = v >= eval_shift(pp); }
    else return v;
  }
}

static long eval_eq(Token **pp) {
  long v = eval_rel(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct2(t, "=="))     { *pp = t->next; v = v == eval_rel(pp); }
    else if (is_punct2(t, "!=")){ *pp = t->next; v = v != eval_rel(pp); }
    else return v;
  }
}

static long eval_band(Token **pp) {
  long v = eval_eq(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct(t, '&'))       { *pp = t->next; v = v & eval_eq(pp); }
    else return v;
  }
}

static long eval_bxor(Token **pp) {
  long v = eval_band(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct(t, '^'))       { *pp = t->next; v = v ^ eval_band(pp); }
    else return v;
  }
}

static long eval_bor(Token **pp) {
  long v = eval_bxor(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct(t, '|'))       { *pp = t->next; v = v | eval_bxor(pp); }
    else return v;
  }
}

static long eval_land(Token **pp) {
  long v = eval_bor(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct2(t, "&&")) {
      *pp = t->next;
      long r = eval_bor(pp);    /* always consume: C's && would short-circuit */
      v = v && r;
    } else return v;
  }
}

static long eval_lor(Token **pp) {
  long v = eval_land(pp);
  for (;;) {
    Token *t = *pp;
    if (is_punct2(t, "||")) {
      *pp = t->next;
      long r = eval_land(pp);   /* short-circuit here would leak tokens */
      v = v || r;
    } else return v;
  }
}

/* eats the whole #if line; the caller has already skipped past the
 * directive name. leftover tokens past the expression are an error */
static long eval_if_expr(Token *pp) {
  long v = eval_lor(&pp);
  if (pp->kind != TK_EOF && !pp->at_bol)
    error_at(pp->loc, "extra tokens after #if expression");
  return v;
}

/* -------- macro expansion -------- */

static void expand_unit(Token **pp, Chain *out, int depth);

/* expand the token slice [start, end) into a fresh chain */
static Token *expand_slice(Token *start, Token *end, int depth) {
  Chain c;
  chain_init(&c);
  while (start != end)
    expand_unit(&start, &c, depth);
  chain_end(&c);
  return c.head;
}

static void expand_unit(Token **pp, Chain *out, int depth) {
  if (depth > 256)
    error_at((*pp)->loc, "macro expansion too deep");

  Token *t = *pp;
  Macro *m = (t->kind == TK_IDENT) ? find_macro(t->name) : NULL;

  if (!m || is_painted(m)) {
    Token *b = (t->kind == TK_IDENT) ? builtin_macro(t) : NULL;
    Token *n = b ? b : t;   /* multi-argutation: copy it */
    Token *c = xmalloc(sizeof(Token));
    *c = *n;
    c->next = NULL;
    *pp = t->next;
    chain_add(out, c);
    return;
  }

  /* NB: chain_add links through the appended token's own ->next
   * field. macro body tokens are shared with the macro definition,
   * so the output chain must never contain them: appending a body
   * token would rewrite its ->next and corrupt the macro for every
   * later use. every append therefore carries a per-chain copy */

  if (!m->is_func) {
    *pp = t->next;
    painting[paint_n++] = m;
    Token *b = m->body;
    while (b != m->body_end)
      expand_unit(&b, out, depth + 1);
    paint_n--;
    return;
  }

  /* function-like: only invoked when '(' follows with no gap */
  Token *open = t->next;
  if (!(open->kind == TK_PUNCT && open->len == 1 &&
        *open->loc == '(' && !open->space)) {
    *pp = t->next;
    chain_add(out, t);
    return;
  }

  /* split the arguments on top-level commas, up to the ')' */
  Token *args[MAX_MACRO_ARGS], *ends[MAX_MACRO_ARGS];
  int nargs = 0;
  int paren = 0;
  Token *as = open->next;
  Token *tt;
  for (tt = open->next; ; tt = tt->next) {
    if (tt->kind == TK_EOF)
      error_at(open->loc, "unterminated macro argument list");
    if (is_punct(tt, '(')) {
      paren++;
      continue;
    }
    if (is_punct(tt, ')')) {
      if (paren == 0) {
        args[nargs] = as;
        ends[nargs] = tt;
        nargs++;
        break;
      }
      paren--;
      continue;
    }
    if (is_punct(tt, ',') && paren == 0) {
      args[nargs] = as;
      ends[nargs] = tt;
      nargs++;
      as = tt->next;
      continue;
    }
  }
  *pp = tt->next;   /* past the ')' */
  if (nargs != m->nparams) {
    int ok_empty = (nargs == 1 && m->nparams == 0 && args[0] == ends[0]);
    if (!ok_empty)
      error_at(open->loc, "macro '%s' needs %d argument(s), got %d",
               m->name, m->nparams, nargs);
  }

  /* arguments are fully expanded before substitution (6.10.3.1),
   * without painting this macro: F(F(1)) must expand the inner one */
  Token *expanded[MAX_MACRO_ARGS];
  for (int i = 0; i < nargs; i++)
    expanded[i] = expand_slice(args[i], ends[i], depth + 1);

  /* substitute and rescan the body under the paint */
  painting[paint_n++] = m;
  Token *b = m->body;
  while (b != m->body_end) {
    int sub = 0;
    if (b->kind == TK_IDENT)
      for (int i = 0; i < m->nparams; i++)
        if (strcmp(b->name, m->params[i]) == 0) {
          for (Token *ct = copy_chain(expanded[i]); ct; ct = ct->next)
            chain_add(out, ct);
          b = b->next;
          sub = 1;
          break;
        }
    if (!sub)
      expand_unit(&b, out, depth + 1);
  }
  paint_n--;
}

/* -------- directives -------- */

static void core_stream(Token *toks, Chain *out, char *srcpath, int depth,
                        int emit_eof);

/* directive names are matched textually: "if" and "else" tokenize
 * as keywords (TK_IF/TK_ELSE), everything else as TK_IDENT */
static int directive_is(Token *t, char *s) {
  return t->len == (int)strlen(s) && memcmp(t->loc, s, t->len) == 0;
}

static void handle_directive(Token **pp, Chain *out, char *srcpath,
                             int depth) {
  Token *t = *pp;              /* the '#' */
  Token *name = t->next;
  int active = active_now();

  if (directive_is(name, "if")) {
    long v = active ? eval_if_expr(name->next) : 0;
    if (cond_n >= MAX_COND_DEPTH)
      error_at(t->loc, "#if nested too deep");
    Cond *c = &conds[cond_n++];
    c->parent_active = active;
    c->active = active && v;
    c->ever_on = c->active;
    c->seen_else = 0;
    *pp = skip_line(&name->next);
    return;
  }

  if (directive_is(name, "ifdef") || directive_is(name, "ifndef")) {
    Token *n = name->next;
    int defined = (n->kind == TK_IDENT &&
                  (find_macro(n->name) || builtin_name(n->name))) != 0;
    int v = directive_is(name, "ifdef") ? defined : !defined;
    if (cond_n >= MAX_COND_DEPTH)
      error_at(t->loc, "#if nested too deep");
    Cond *c = &conds[cond_n++];
    c->parent_active = active;
    c->active = active && v;
    c->ever_on = c->active;
    c->seen_else = 0;
    *pp = skip_line(&n->next);
    return;
  }

  if (directive_is(name, "elif")) {
    if (!cond_n)
      error_at(t->loc, "stray #elif");
    Cond *c = &conds[cond_n - 1];
    if (c->ever_on)
      error_at(t->loc, "#elif after a taken branch");
    long v = c->parent_active ? eval_if_expr(name->next) : 0;
    int branch = c->parent_active && v;
    c->active = branch;
    c->ever_on = branch;
    *pp = skip_line(&name->next);
    return;
  }

  if (directive_is(name, "else")) {
    if (!cond_n)
      error_at(t->loc, "stray #else");
    Cond *c = &conds[cond_n - 1];
    if (c->seen_else)
      error_at(t->loc, "duplicate #else");
    c->seen_else = 1;
    c->active = c->parent_active && !c->ever_on;
    c->ever_on = 1;
    *pp = skip_line(&name->next);
    return;
  }

  if (directive_is(name, "endif")) {
    if (!cond_n)
      error_at(t->loc, "stray #endif");
    cond_n--;
    *pp = skip_line(&name->next);
    return;
  }

  /* everything below is inert inside a skipped branch */
  if (!active) {
    *pp = skip_line(&name->next);
    return;
  }

  if (directive_is(name, "define")) {
    Token *d = name->next;
    if (d->kind != TK_IDENT)
      error_at(d->loc, "expected macro name after #define");
    Macro *m = find_macro(d->name);
    if (!m) {
      m = xmalloc(sizeof(Macro));
      m->name = d->name;
      m->next = macros;
      macros = m;
    }
    m->nparams = 0;
    m->params = NULL;
    m->is_func = 0;
    Token *b = d->next;
    if (b->kind == TK_PUNCT && *b->loc == '(' && !b->space) {
      m->is_func = 1;
      b = b->next;
      if (!(b->kind == TK_PUNCT && *b->loc == ')' && b->len == 1)) {
        for (;;) {
          if (b->kind == TK_PUNCT && b->len == 3 &&
              memcmp(b->loc, "...", 3) == 0)
            error_at(b->loc, "variadic macros not supported");
          if (b->kind != TK_IDENT)
            error_at(b->loc, "expected parameter name");
          m->params = xrealloc(m->params,
                               sizeof(char *) * (m->nparams + 1));
          m->params[m->nparams++] = b->name;
          b = b->next;
          if (b->kind == TK_PUNCT && *b->loc == ')' && b->len == 1)
            break;
          if (b->kind == TK_PUNCT && *b->loc == ',' && b->len == 1) {
            b = b->next;
            continue;
          }
          error_at(b->loc, "expected ',' or ')' in macro parameters");
        }
        b = b->next;
      }
    }
    m->body = b;
    m->body_end = skip_line(&b);
    for (Token *x = m->body; x != m->body_end; x = x->next)
      if (x->kind == TK_PUNCT && *x->loc == '#' && x->len == 1)
        error_at(x->loc, "stringize/paste not supported in macros");
    *pp = m->body_end;
    return;
  }

  if (directive_is(name, "undef")) {
    Token *n = name->next;
    if (n->kind == TK_IDENT) {
      Macro **link = &macros;
      for (; *link; link = &(*link)->next)
        if (strcmp((*link)->name, n->name) == 0) {
          *link = (*link)->next;
          break;
        }
    }
    *pp = skip_line(&n->next);
    return;
  }

  if (directive_is(name, "include")) {
    Token *p = name->next;
    char *inc = NULL;
    int angled = 0;
    if (p->kind == TK_STR) {
      inc = p->str;
    } else if (is_punct(p, '<')) {
      Token *gt = p;
      while (gt->kind != TK_EOF && !(is_punct(gt, '>')))
        gt = gt->next;
      if (gt->kind == TK_EOF)
        error_at(p->loc, "unterminated <...> include");
      inc = xstrndup(p->loc, gt->loc + 1 - p->loc);
      angled = 1;
    } else {
      error_at(p->loc, "expected file name after #include");
    }
    *pp = skip_line(&p->next);

    if (depth >= MAX_INCLUDE_DEPTH)
      error_at(t->loc, "#include nested too deep");

    /* "..." resolves against the including file's directory first,
     * "<...>" goes straight to the -I dirs */
    char *found = NULL;
    char cand[512];
    if (!angled) {
      char *slash = strrchr(srcpath, '/');
      if (slash) {
        int dir_len = slash - srcpath;
        snprintf(cand, sizeof(cand), "%.*s/%s", dir_len, srcpath, inc);
      } else {
        snprintf(cand, sizeof(cand), "%s", inc);
      }
      FILE *f = fopen(cand, "r");
      if (f) {
        fclose(f);
        found = cand;
      }
    }
    for (int i = 0; !found && i < incdir_n; i++) {
      snprintf(cand, sizeof(cand), "%s/%s", incdirs[i], inc);
      FILE *f = fopen(cand, "r");
      if (f) {
        fclose(f);
        found = cand;
      }
    }
    if (!found)
      error_at(t->loc, "cannot open include file '%s'", inc);

    char *save_file = cur_file;
    char *buf = read_file(found);
    cur_file = found;
    core_stream(tokenize(buf), out, found, depth + 1, 0);
    cur_file = save_file;
    return;
  }

  if (directive_is(name, "error")) {
    Token *n = name->next;
    if (n->kind == TK_EOF || n->at_bol)
      error_at(t->loc, "#error");
    char *buf = xmalloc(64);
    int msg_len = 0, cap = 64;
    for (; n->kind != TK_EOF && !n->at_bol; n = n->next) {
      if (msg_len + n->len + 2 > cap) {
        cap = msg_len + n->len + 2;
        buf = xrealloc(buf, cap);
      }
      memcpy(buf + msg_len, n->loc, n->len);
      msg_len += n->len;
      buf[msg_len++] = ' ';
    }
    buf[msg_len] = '\0';
    error_at(t->loc, "#error %s", buf);
  }

  if (directive_is(name, "pragma") || directive_is(name, "line")) {
    *pp = skip_line(&name->next);
    return;
  }

  if (name->kind == TK_IDENT)
    error_at(name->loc, "invalid preprocessing directive '#%s'", name->name);

  *pp = skip_line(&name->next);   /* null directive: a lone '#' */
}

/* scan a skipped branch for the conditional structure, leaving
 * everything else alone; returns when the stream is live again */
static void skip_to_active(Token **pp, Chain *out, char *srcpath, int depth) {
  Token *t = *pp;
  for (;;) {
    if (t->kind == TK_EOF)
      break;
    if (t->kind == TK_PUNCT && *t->loc == '#' && t->at_bol) {
      handle_directive(&t, out, srcpath, depth);
      if (active_now())
        break;
      continue;
    }
    t = t->next;
  }
  *pp = t;
}

static void core_stream(Token *toks, Chain *out, char *srcpath, int depth,
                        int emit_eof) {
  Token *t = toks;
  for (; t->kind != TK_EOF; ) {
    if (t->kind == TK_PUNCT && *t->loc == '#' && t->at_bol) {
      handle_directive(&t, out, srcpath, depth);
      continue;
    }
    if (!active_now()) {
      skip_to_active(&t, out, srcpath, depth);
      continue;
    }
    expand_unit(&t, out, 0);
  }
  if (emit_eof)
    chain_add(out, t);
}

Token *preprocess(Token *toks, char *srcpath) {
  cur_file = srcpath;
  counter = 0;
  Chain out;
  chain_init(&out);
  core_stream(toks, &out, srcpath, 0, 1);
  chain_end(&out);
  if (cond_n > 0)
    error("unterminated #if");
  return out.head;
}