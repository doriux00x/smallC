/* dump mode: enum front-end coverage */
enum primary { P_RED, P_GREEN = 5, P_BLUE = P_GREEN + 1 };
enum { FIRST, SECOND = FIRST + 10 };
typedef enum primary Primary;
enum state { ON, OFF };

enum state g_state = ON;
enum primary g_p = P_BLUE;

int values[P_BLUE];

enum state flip(void);
enum state flip(void) { return g_state ? OFF : ON; }

int f(void) {
  enum state s = g_state;
  int n = sizeof(enum state) + sizeof(s);
  int i = 0;
  while (i < P_BLUE) {
    values[i] = i * P_GREEN;
    i++;
  }
  if (s == OFF)
    return 1;
  return n + values[2] + flip() + SECOND;
}