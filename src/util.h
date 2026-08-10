#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

extern char *g_src;

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrndup(char *s, size_t n);
char *xstrdup(char *s);

void error(char *fmt, ...) __attribute__((noreturn));
void error_at(char *loc, char *fmt, ...) __attribute__((noreturn));

char *read_file(char *path);

#endif
