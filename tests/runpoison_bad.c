/* must fail in both compilers: poison.h poisons sneaky */
#include <poison.h>
int main(void) {
  return sneaky;
}
