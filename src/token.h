#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
  TK_EOF,
  TK_IDENT,
  TK_NUM,
  TK_STR,

  TK_VOID, TK_CHAR, TK_SHORT, TK_INT, TK_LONG,
  TK_SIGNED, TK_UNSIGNED, TK_FLOAT, TK_DOUBLE,
  TK_STRUCT, TK_UNION, TK_ENUM,
  TK_TYPEDEF, TK_EXTERN, TK_STATIC,
  TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_RETURN,
  TK_SIZEOF, TK_BREAK, TK_CONTINUE, TK_DO,

  TK_PUNCT,
} TokenKind;

typedef struct Token Token;

struct Token {
  TokenKind kind;
  Token *next;
  int val;       /* numeric value when TK_NUM */
  char *loc;     /* start of token in source buffer */
  int len;       /* byte length of token text */
  char *name;    /* allocated copy, TK_IDENT only */
  char *str;     /* decoded string, TK_STR only */
  int str_len;
};

Token *tokenize(char *p);
char *token_kind_name(TokenKind k);

#endif
