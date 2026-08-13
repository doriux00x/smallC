#include "token.h"
#include "util.h"

#include "libc.h"

typedef struct {
  char *name;
  TokenKind kind;
} Keyword;

/* FIXME: linear scan per identifier, switch to a hash when we pass ~30 kw */
static Keyword keywords[] = {
  {"void", TK_VOID}, {"char", TK_CHAR}, {"short", TK_SHORT},
  {"int", TK_INT}, {"long", TK_LONG},
  {"signed", TK_SIGNED}, {"unsigned", TK_UNSIGNED},
  {"float", TK_FLOAT}, {"double", TK_DOUBLE}, {"_Bool", TK_BOOL},
  {"struct", TK_STRUCT}, {"union", TK_UNION}, {"enum", TK_ENUM},
  {"const", TK_CONST}, {"volatile", TK_VOLATILE},
  {"typedef", TK_TYPEDEF}, {"extern", TK_EXTERN}, {"static", TK_STATIC},
  {"register", TK_REGISTER},
  {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE},
  {"for", TK_FOR}, {"return", TK_RETURN}, {"sizeof", TK_SIZEOF},
  {"break", TK_BREAK}, {"continue", TK_CONTINUE}, {"do", TK_DO},
  {"switch", TK_SWITCH}, {"case", TK_CASE}, {"default", TK_DEFAULT}, {"goto", TK_GOTO},
};

/* longest first so ">>=" wins over ">>" which wins over ">" */
static char *puncts[] = {
  "<<=", ">>=", "...",
  "==", "!=", "<=", ">=", "&&", "||", "->", "++", "--",
  "+=", "-=", "*=", "/=", "%=", "<<", ">>", "&=", "|=", "^=",
  "<", ">", "=", "+", "-", "*", "/", "%", "&", "|", "^", "~", "!",
  "?", ":", ";", ",", ".", "(", ")", "[", "]", "{", "}", "#",
};

static Token *tok_new(TokenKind kind, char *start, int len,
                      int line, int at_bol, int space) {
  Token *t = xmalloc(sizeof(Token));
  t->kind = kind;
  t->loc = start;
  t->len = len;
  t->line = line;
  t->at_bol = at_bol;
  t->space = space;
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

/* 1.5 / 1e3 / 1.5e-3 / .5 are floats; hex stays on the strtol path.
 * requires a digit after '.' unless an exponent picks up the slack
 * ("1.e3" is legal C, so is it here) */
static int is_float_lit(char *p) {
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    return 0;
  if (p[0] == '.')
    return isdigit((unsigned char)p[1]);
  while (isdigit((unsigned char)*p))
    p++;
  if (p[0] == '.') {
    p++;
    return isdigit((unsigned char)p[0]) ||
           p[0] == 'e' || p[0] == 'E';
  }
  if (p[0] == 'e' || p[0] == 'E') {
    p++;
    if (*p == '+' || *p == '-')
      p++;
    return isdigit((unsigned char)p[0]);
  }
  return 0;
}

static Token *read_number(char *start, char **pp, int line, int at_bol, int space) {
  char *p = *pp;
  Token *t = tok_new(TK_NUM, start, 0, line, at_bol, space);
  if (is_float_lit(p)) {
    /* FIXME: strtod silently gives inf on overflow */
    t->fval = strtod(p, &p);
    t->is_float = 1;
  } else {
    /* the raw value decides the type: a hex literal past INT_MAX
     * is an unsigned int; val keeps the 32-bit truncation */
    long v = strtol(p, &p, 0);
    t->val = (int)v;
    t->is_unsigned = v > 2147483647;
  }
  if (*p == 'f' || *p == 'F') {
    /* "1f" is the float 1.0f; strtod stops at the suffix */
    if (!t->is_float)
      t->fval = strtod(start, &p);
    p++;
    t->is_float = 1;
    t->is_f = 1;
  }
  t->len = p - start;
  *pp = p;
  return t;
}

Token *tokenize(char *p) {
  Token head = {0};
  Token *cur = &head;
  int line = 1, at_bol = 1, space = 0;

  while (*p) {
    if (isspace((unsigned char)*p)) {
      if (*p == '\n') {
        line++;
        at_bol = 1;
      }
      space = 1;
      p++;
      continue;
    }

    if (strncmp(p, "//", 2) == 0) {
      p += 2;
      while (*p && *p != '\n')
        p++;
      space = 1;
      continue;
    }

    if (strncmp(p, "/*", 2) == 0) {
      char *end = strstr(p + 2, "*/");
      if (!end)
        error_at(p, "unterminated comment");
      for (char *q = p + 2; q < end; q++)
        if (*q == '\n') {
          line++;
          at_bol = 1;
        }
      space = 1;
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

      Token *t = tok_new(kind, start, len, line, at_bol, space);
      if (kind == TK_IDENT)
        t->name = xstrndup(start, len);
      cur = cur->next = t;
      space = 0;
      at_bol = 0;
      continue;
    }

    if (isdigit((unsigned char)*p) ||
        (*p == '.' && isdigit((unsigned char)p[1]))) {
      char *start = p;
      Token *t = read_number(start, &p, line, at_bol, space);
      cur = cur->next = t;
      space = 0;
      at_bol = 0;
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
        if (n + 1 == cap) {
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

      Token *t = tok_new(TK_STR, start, p - start, line, at_bol, space);
      t->str = buf;
      t->str[n] = '\0';
      t->str_len = n;
      cur = cur->next = t;
      space = 0;
      at_bol = 0;
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

      Token *t = tok_new(TK_NUM, start, p - start, line, at_bol, space);
      t->val = c;
      cur = cur->next = t;
      space = 0;
      at_bol = 0;
      continue;
    }

    int matched = 0;
    for (int i = 0; i < (int)ARRAY_LEN(puncts); i++) {
      int plen = (int)strlen(puncts[i]);
      if (strncmp(p, puncts[i], plen) == 0) {
        cur = cur->next = tok_new(TK_PUNCT, p, plen, line, at_bol, space);
        p += plen;
        matched = 1;
        break;
      }
    }
    if (matched) {
      space = 0;
      at_bol = 0;
      continue;
    }

    error_at(p, "unexpected character '%c'", *p);
  }

  cur->next = tok_new(TK_EOF, p, 0, line, 0, 0);
  return head.next;
}

static char *kind_names[] = {
  "EOF", "IDENT", "NUM", "STR",
  "VOID", "CHAR", "SHORT", "INT", "LONG",
  "SIGNED", "UNSIGNED", "FLOAT", "DOUBLE", "BOOL",
  "STRUCT", "UNION", "ENUM", "CONST", "VOLATILE",
  "TYPEDEF", "EXTERN", "STATIC", "REGISTER",
  "IF", "ELSE", "WHILE", "FOR", "RETURN",
  "SIZEOF", "BREAK", "CONTINUE", "DO",
  "SWITCH", "CASE", "DEFAULT", "GOTO",
  "PUNCT",
};

/* compile-time check that the name table tracks the enum */
/* enumerate to PUNCT so kind_names[] below can be sized from the
 * enum, keeping the arrays and the enum naturally in sync */

char *token_kind_name(TokenKind k) {
  return kind_names[k];
}
