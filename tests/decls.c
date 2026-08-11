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

/* brace initializers parse and flatten to leaves */
int n3[3] = {1, 2, 3};
int nflat[2][2] = {1, 2, 3, 4};
int nflex[] = {1, 2};
char nstr[6] = "hello";
char nstr2[] = "hi";
char ngrid[][4] = {"ab", "cd"};
double ndbl[2] = {1.5, 2.5};
int nzero[3] = {0};
