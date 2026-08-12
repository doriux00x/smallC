/* dump mode: static/extern storage classes */
extern int ext_var;
int ext_var = 10;
static int s_int = 1;
static int s_bss;
static const char *s_str = "s";
static double s_dbl = 1.5;
static int s_arr[2] = { 1, 2 };

extern int proto(int);
int proto(int x) { return x + ext_var; }
static int helper(int x) { return x * 2; }
extern int use_proto(int y);

int f(void) {
  int n = s_int + s_bss + s_arr[1] + proto(1) + use_proto(2) + helper(3);
  return n;
}