/* function pointer and nested declarator tests - the meanest part of C */

int (*fp1)(int);
int (*fp2)(int) = 0;
int *(*fp3)(char *, int);
int (*fparr[4])(int);
int (*fpmem[2][2])(void);
int (*fp5)(int (*cb)(int), char *);
int *(*(*deep)(int))(char);
int (*getcb(void))(int);
void (*signal_handler(int sig, void (*h)(int)))(int);
int apply(int (*fn)(int), int x);
int takes_abstract(int (*)(int));
char *(*word_ops[6])(char *);

int use_them(int x) {
  int (*local)(int);
  local = fp1;
  return local(x);
}