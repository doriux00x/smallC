/* enum: tags, enumerators as parse-time int constants, folding of
 * constant exprs, use in switch cases, array dims, globals. */

enum color { RED, GREEN, BLUE };
enum flags { NONE = 0, BIT1 = 1, BIT2 = 2, BOTH = BIT1 | BIT2 };
enum neg { MINUS = -3, MINUS2, SEQ = MINUS2 + 5 };
enum { ANON1, ANON2 };                       /* anonymous enum */
enum calced { A = 1 + 2, B = A * 2, C = sizeof(int) + 1, D = 1 << 4 };
typedef enum color Color;
enum ecolor { EC1 = 10, EC2 };
enum unused { SUV = 3, SEDAN, HATCH };

enum color g_col = GREEN;
int g_idx = BLUE;
enum flags g_flags = BOTH;

int table[3] = { RED, GREEN, BLUE };

enum color pick(int i) {
  if (i == 0) return RED;
  if (i == 1) return GREEN;
  return BLUE;
}

int main(void) {
  int check = 0;

  check += (RED == 0 && GREEN == 1 && BLUE == 2);
  check += (MINUS == -3 && MINUS2 == -2 && SEQ == 3);
  check += (ANON1 == 0 && ANON2 == 1);
  check += (A == 3 && B == 6 && C == 5 && D == 16);
  check += (BIT1 == 1 && BIT2 == 2 && BOTH == 3);

  check += (g_col == GREEN && g_idx == 2 && g_flags == 3);
  check += (pick(0) == 0 && pick(2) == 2);
  check += (table[0] == 0 && table[1] == 1 && table[2] == 2);

  enum color c = BLUE;
  check += (sizeof(c) == 4 && sizeof(enum color) == 4);

  {
    enum nested { N1 = 7 };
    check += N1 == 7;
  }

  int sw = 0;
  switch (SEQ) {
  case MINUS2: sw = 1; break;
  case SEQ:    sw = 2; break;
  default:     sw = 3; break;
  }
  check += sw == 2;

  Color cc = GREEN;
  check += (cc == 1 && sizeof(enum ecolor) == 4 && EC2 == 11);

  int sc = 0;
  enum { POS, NEG };
  if (NEG) sc = 1;
  check += sc == 1;

  if (check != 13)
    return check;
  printf("runenum ok\n");
  return 0;
}