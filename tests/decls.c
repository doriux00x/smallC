/* variable declarations the parser should handle */

int a;
int b, c, d;
unsigned int flags;
long counter;
unsigned long long big_counter;
char *name;
char *first_name, *last_name;
int *pointers[8];
unsigned char bytes[256];
void *data;
short small;
float ratio;
double precise;

/* comments and a string literal must not break the lexer */
char *message;   /* would have a literal init once ND_DECL gets one */
