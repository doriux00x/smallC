#ifndef LIBC_H
#define LIBC_H

/* what libc looks like from a self-hosted compiler's point of view:
 * fixed prototypes for the handful of functions the compiler calls.
 * the printf family is declared variadic with `...`; on x86-64 SysV
 * the extra args are passed in the same registers as fixed params,
 * so libc reads them exactly as the format string demands. */

typedef unsigned long size_t;
typedef struct _IO_FILE FILE;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* errno in glibc is per-thread TLS; the header hides that behind a
 * function call, exactly as <errno.h> does */
int *__errno_location(void);
#define errno (*__errno_location())

/* 34 on Linux, checked against <errno.h> at build time */
#define ERANGE 34

extern FILE *stderr;

void *malloc(size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int status);

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *p, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *hay, const char *needle);

double strtod(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);


int isalpha(int c);
int isalnum(int c);
int isdigit(int c);
int isxdigit(int c);
int isspace(int c);
int tolower(int c);

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);

FILE *fopen(const char *path, const char *mode);
char *realpath(const char *path, char *resolved);
int fclose(FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fputc(int c, FILE *f);

int mkdir(char *path, int mode);

/* the build-time stamp for __DATE__ and __TIME__: broken-down local
 * time, the same fields gcc's preprocessor reads (time_t is long on
 * this platform) */
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
};
long time(long *timer);
struct tm *localtime(const long *timer);

#endif
