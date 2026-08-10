#include "token.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *name;
  TokenKind kind;
} Keyword;

/* FIXME: linear scan per identifier, switch to a hash when we pass ~30 kw */
static Keyword keywords[] = {
  {"void", TK_VOID}, {"char", TK_CHAR}, {"short", TK_SHORT},
  {"int", TK_INT}, {"long", TK_LONG},
  {"signed", TK_SIGNED}, {"unsigned", TK_UNSIGNED},
  {"float", TK_FLOAT}, {"double", TK_DOUBLE},
  {"struct", TK_STRUCT}, {"union", TK_UNION}, {"enum", TK_ENUM},
  {"typedef", TK_TYPEDEF}, {"extern", TK_EXTERN}, {"static", TK_STATIC},
  {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE},
  {"for", TK_FOR}, {"return", TK_RETURN}, {"sizeof", TK_SIZEOF},
  {"break", TK_BREAK}, {"continue", TK_CONTINUE},
};

/* longest first so ">>=" wins over ">>" which wins over ">" */
static char *puncts[] = {
  "<<=", ">>=", "...",
  "==", "!=", "<=", ">=", "&&", "||", "->", "++", "--",
  "+=", "-=", "*=", "/=", "%=", "<<", ">>", "&=", "|=", "^=",
  "<", ">", "=", "+", "-", "*", "/", "%", "&", "|", "^", "~", "!",
  "?", ":", ";", ",", ".", "(", ")", "[", "]", "{", "}", "#",
};

static Token *tok_new(TokenKind kind, char *start, int len) {
  Token *t = xmalloc(sizeof(Token));
  t->kind = kind;
  t->loc = start;
  t->len = len;
  return t;
}

/* decodes one escape sequence; pp points at the backslash, advanced past it */
static char decode_escape(char *start, char **pp) {
  char *p = *pp;
  p++;
  char c = 0;
  switch (*p) {
    case 'n':  c = '\n'; break;
    case 't':  c = '\t'; break;
    case 'r':  c = '\r'; break;
    case '0':  c = '\0'; break;
    case '\\': c = '\\'; break;
    case '\'': c = '\''; break;
    case '"':  c = '"';  break;
    default:
      error_at(start, "unknown escape sequence '\\%c'", *p);
  }
  *pp = p + 1;
  return c;
}

Token *tokenize(char *p) {
  Token head = {0};
  Token *cur = &head;

  while (*p) {
    if (isspace((unsigned char)*p)) {
      p++;
      continue;
    }

    if (strncmp(p, "//", 2) == 0) {
      p += 2;
      while (*p && *p != '\n')
        p++;
      continue;
    }

    if (strncmp(p, "/*", 2) == 0) {
      char *end = strstr(p + 2, "*/");
      if (!end)
        error_at(p, "unterminated comment");
      p = end + 2;
      continue;
    }

    if (isalpha((unsigned char)*p) || *p == '_') {
      char *start = p;
      p++;
      while (isalnum((unsigned char)*p) || *p == '_')
        p++;
      int len = p - start;

      TokenKind kind = TK_IDENT;
      for (int i = 0; i < (int)ARRAY_LEN(keywords); i++)
        if (len == (int)strlen(keywords[i].name) &&
            memcmp(start, keywords[i].name, len) == 0) {
          kind = keywords[i].kind;
          break;
        }

      Token *t = tok_new(kind, start, len);
      if (kind == TK_IDENT)
        t->name = xstrndup(start, len);
      cur = cur->next = t;
      continue;
    }

    if (isdigit((unsigned char)*p)) {
      char *start = p;
      Token *t = tok_new(TK_NUM, start, 0);
      /* FIXME: strtol clamps on overflow, and we truncate to int */
      t->val = (int)strtol(p, &p, 0);
      t->len = p - start;
      cur = cur->next = t;
      continue;
    }

    if (*p == '"') {
      char *start = p;
      p++;
      char *buf = xmalloc(32);
      int cap = 32, n = 0;
      while (*p != '"') {
        if (*p == '\0' || *p == '\n')
          error_at(start, "unterminated string literal");
        if (n == cap) {
          cap *= 2;
          buf = xrealloc(buf, cap);
        }
        if (*p == '\\') {
          buf[n++] = decode_escape(start, &p);
        } else {
          buf[n++] = *p++;
        }
      }
      p++; /* closing quote */

      Token *t = tok_new(TK_STR, start, p - start);
      t->str = buf;
      t->str[n] = '\0';
      t->str_len = n;
      cur = cur->next = t;
      continue;
    }

    if (*p == '\'') {
      char *start = p;
      p++;
      if (*p == '\0' || *p == '\n' || *p == '\'')
        error_at(start, "empty char literal");
      int c;
      if (*p == '\\')
        c = decode_escape(start, &p);
      else {
        c = *p;
        p++;
      }
      if (*p != '\'')
        error_at(start, "multi-char constants not supported");
      p++;

      Token *t = tok_new(TK_NUM, start, p - start);
      t->val = c;
      cur = cur->next = t;
      continue;
    }

    int matched = 0;
    for (int i = 0; i < (int)ARRAY_LEN(puncts); i++) {
      int plen = (int)strlen(puncts[i]);
      if (strncmp(p, puncts[i], plen) == 0) {
        cur = cur->next = tok_new(TK_PUNCT, p, plen);
        p += plen;
        matched = 1;
        break;
      }
    }
    if (matched)
      continue;

    error_at(p, "unexpected character '%c'", *p);
  }

  cur->next = tok_new(TK_EOF, p, 0);
  return head.next;
}

static char *kind_names[] = {
  "EOF", "IDENT", "NUM", "STR",
  "VOID", "CHAR", "SHORT", "INT", "LONG",
  "SIGNED", "UNSIGNED", "FLOAT", "DOUBLE",
  "STRUCT", "UNION", "ENUM",
  "TYPEDEF", "EXTERN", "STATIC",
  "IF", "ELSE", "WHILE", "FOR", "RETURN",
  "SIZEOF", "BREAK", "CONTINUE",
  "PUNCT",
};

/* compile-time check that the name table tracks the enum */
typedef char check_enum_size[ARRAY_LEN(kind_names) == TK_PUNCT + 1 ? 1 : -1];

char *token_kind_name(TokenKind k) {
  return kind_names[k];
}
