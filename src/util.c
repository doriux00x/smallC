#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

char *g_src;

void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (!p) {
    perror("malloc");
    exit(1);
  }
  return p;
}

void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) {
    perror("realloc");
    exit(1);
  }
  return q;
}

char *xstrndup(char *s, size_t n) {
  char *p = xmalloc(n + 1);
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

char *xstrdup(char *s) {
  return xstrndup(s, strlen(s));
}

void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

// points at the offending token, prints the source line and a caret
void error_at(char *loc, char *fmt, ...) {
  int line = 1;
  char *line_start = g_src;
  for (char *p = g_src; p < loc; p++) {
    if (*p == '\n') {
      line++;
      line_start = p + 1;
    }
  }

  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "line %d: ", line);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");

  int col = loc - line_start;
  fprintf(stderr, "  %.*s\n", (int)strcspn(line_start, "\n"), line_start);
  for (int i = 0; i < col + 2; i++)
    fputc(' ', stderr);
  fprintf(stderr, "^\n");
  exit(1);
}

char *read_file(char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    error("cannot open %s: %s", path, strerror(errno));

  if (fseek(fp, 0, SEEK_END) != 0)
    error("cannot seek in %s", path);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char *buf = xmalloc(sz + 2);
  if (fread(buf, 1, sz, fp) < (size_t)sz)
    error("short read on %s", path);
  fclose(fp);

  // two NULs so the lexer can safely do 2-char lookahead at EOF
  buf[sz] = '\0';
  buf[sz + 1] = '\0';
  return buf;
}
