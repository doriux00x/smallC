#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
  TK_EOF,
  TK_IDENT,
  TK_NUM,
  TK_STR,

  TK_VOID, TK_CHAR, TK_SHORT, TK_INT, TK_LONG,
  TK_SIGNED, TK_UNSIGNED, TK_FLOAT, TK_DOUBLE, TK_BOOL,
  TK_STRUCT, TK_UNION, TK_ENUM, TK_CONST, TK_VOLATILE,
  TK_TYPEDEF, TK_EXTERN, TK_STATIC, TK_REGISTER,
  TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_RETURN,
  TK_SIZEOF, TK_BREAK, TK_CONTINUE, TK_DO,
  TK_SWITCH, TK_CASE, TK_DEFAULT, TK_GOTO,

  TK_PUNCT,
} TokenKind;

typedef struct Token Token;

struct Token {
  TokenKind kind;
  Token *next;
  int val;       /* numeric value when TK_NUM and not is_float */
  int is_float;  /* TK_NUM: floating literal */
  int is_f;      /* TK_NUM: the f/F suffix, a float (not double) literal */
  double fval;   /* TK_NUM float value */
  char *loc;     /* start of token in source buffer */
  int len;       /* byte length of token text */
  int line;      /* 1-based source line (for __LINE__ and errors) */
  int at_bol;    /* first token on its line (directive detection) */
  int space;     /* whitespace or comment before this token */
  char *name;    /* allocated copy, TK_IDENT only */
  char *str;     /* decoded string, TK_STR only */
  int str_len;
};

Token *tokenize(char *p);
char *token_kind_name(TokenKind k);

#endif
