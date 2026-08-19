#ifndef PREPROC_H
#define PREPROC_H

typedef struct Token Token;

void add_include_dir(char *dir);
void define_macro_cli(char *def);
void undef_macro_cli(char *name);
Token *preprocess(Token *toks, char *srcpath);

#endif