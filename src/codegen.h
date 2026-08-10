#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

void resolve(Node *prog);
void codegen(Node *prog, char *outpath);

#endif