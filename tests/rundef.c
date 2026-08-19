/* the command-line macro options, exercised by the Makefile line
 * `-D FLAG=7 -D OTHER -U __linux__`: FLAG must arrive as the single
 * integer token 7 (so the #if evaluator's single-token-macro path
 * folds it), OTHER is a bare define that expands to nothing but is
 * defined, and -U must revoke a predefined macro the way gcc lets
 * -U and #undef kill __linux__ */

int check = 0;

#if FLAG != 7
#error FLAG must have the value 7
#endif

#ifndef OTHER
#error OTHER must be defined
#endif

#ifdef __linux__
#error -U __linux__ must have revoked it
#endif

int main(void) {
#if FLAG == 7
  check += 1;
#else
  check += 1000;
#endif
#ifdef OTHER
  check += 1;
#endif
#ifndef __linux__
  check += 1;
#endif
  return check != 3;
}