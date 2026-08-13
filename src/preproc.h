#ifndef PREPROC_H
#define PREPROC_H

typedef struct Token Token;

void add_include_dir(char *dir);
Token *preprocess(Token *toks, char *srcpath);

#endif