/* static/extern: static vars and functions stay local to the unit,
 * extern declares without defining; both must round-trip through
 * codegen and link cleanly. */

extern int global_ext;      /* defined below */
int global_ext = 10;

static int hidden = 5;
static int hidden_uninit;
static const int hidden_const = 7;
static int hidden_arr[3] = { 1, 2, 3 };

extern int ext_fn(int);
static int local_helper(int x) { return x * 2; }
int ext_fn(int x) { return x + global_ext; }

static double hidden_d = 2.5;
static char *hidden_s = "ss";

int main(void) {
  int check = 0;

  check += (global_ext == 10);
  hidden_uninit = 42;
  check += (hidden == 5 && hidden_uninit == 42 && hidden_const == 7);
  check += (hidden_arr[0] + hidden_arr[1] + hidden_arr[2] == 6);
  check += (ext_fn(1) == 11);
  check += (local_helper(4) == 8);
  check += (hidden_d == 2.5 && hidden_s[1] == (int)'s' && hidden_s[0] == (int)'s');

  int *local_ref = hidden_arr;
  check += local_ref[2] == 3;

  if (check != 7)
    return check;
  printf("runstatic ok\n");
  return 0;
}