/* only fed to `smallcc -t`, never parsed:
 * strings, escapes, numbers and tricky operator sequences */

char *a = "hello /* not a comment */ world";
char *b = "tab\t newline\n quote\" backslash\\ nul\0";
int hex = 0xdeadbeef;
int oct = 0755;
int shift = 1 << 4;
int cmp = a != b;
int arith = a >= b;
char ch = 'a';
char esc = '\n';
