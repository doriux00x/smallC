#include "parser.h"
#include "token.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Token *tok;

static Token *consume(TokenKind k) {
  if (tok->kind != k)
    return NULL;
  Token *t = tok;
  tok = tok->next;
  return t;
}

static int at(TokenKind k) {
  return tok->kind == k;
}

static Token *expect(TokenKind k, char *what) {
  if (tok->kind != k)
    error_at(tok->loc, "expected %s", what);
  Token *t = tok;
  tok = tok->next;
  return t;
}

static Token *expect_ident(char *what) {
  return expect(TK_IDENT, what);
}

static int is_punct(char *op) {
  return tok->kind == TK_PUNCT &&
         tok->len == (int)strlen(op) &&
         memcmp(tok->loc, op, tok->len) == 0;
}

static int consume_punct(char *op) {
  if (!is_punct(op))
    return 0;
  tok = tok->next;
  return 1;
}

static void expect_punct(char *op) {
  if (!is_punct(op))
    error_at(tok->loc, "expected '%s'", op);
  tok = tok->next;
}

/* any run of type keywords: "unsigned long long" etc. */
static Type *parse_typespec(void) {
  int is_unsigned = 0;
  int longs = 0;
  Type *t = NULL;

  for (;;) {
    if (consume(TK_SIGNED))    { is_unsigned = 0; continue; }
    if (consume(TK_UNSIGNED))  { is_unsigned = 1; continue; }
    if (consume(TK_LONG))      { longs++;         continue; }
    if (consume(TK_VOID))      { t = type_new(TY_VOID);   continue; }
    if (consume(TK_CHAR))      { t = type_new(TY_CHAR);   continue; }
    if (consume(TK_SHORT))     { t = type_new(TY_SHORT);  continue; }
    if (consume(TK_INT))       { t = type_new(TY_INT);    continue; }
    if (consume(TK_FLOAT))     { t = type_new(TY_FLOAT);  continue; }
    if (consume(TK_DOUBLE))    { t = type_new(TY_DOUBLE); continue; }
    break;
  }

  if (!t)
    t = type_new(longs ? TY_LONG : TY_INT);
  if (longs >= 2)
    t->is_longlong = 1;
  t->is_unsigned = is_unsigned;
  return t;
}

static Node *node_new(NodeKind k) {
  Node *n = xmalloc(sizeof(Node));
  n->kind = k;
  return n;
}

/* currently just "name" or "name[N]"; parens/function types come later */
static Node *parse_declarator(Type *base) {
  while (consume_punct("*"))
    base = ptr_to(base);

  Token *ident = expect_ident("identifier");

  if (consume_punct("[")) {
    if (!at(TK_NUM))
      error_at(tok->loc, "expected array size");
    int len = tok->val;
    tok = tok->next;
    expect_punct("]");
    base = array_of(base, len);
  }

  Node *n = node_new(ND_DECL);
  n->name = ident->name;
  n->type = base;
  return n;
}

static Node *parse_declaration(void) {
  Type *base = parse_typespec();

  Node head = {0};
  Node **link = &head.next;

  for (;;) {
    Node *n = parse_declarator(base);
    *link = n;
    link = &n->next;

    if (is_punct("("))
      error_at(tok->loc, "function definitions not implemented yet");

    if (!consume_punct(","))
      break;
  }

  expect_punct(";");
  return head.next;
}

static Node *parse_program(void) {
  Node head = {0};
  Node **link = &head.next;

  while (tok->kind != TK_EOF) {
    if (consume_punct(";"))
      continue;   /* stray semicolon, legal at top level */

    Node *decls = parse_declaration();
    while (decls) {
      *link = decls;
      link = &decls->next;
      decls = decls->next;
    }
  }
  return head.next;
}

Node *parse(Token *t) {
  tok = t;
  return parse_program();
}
