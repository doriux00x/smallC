/* __DATE__ is "Mmm dd yyyy" and __TIME__ is "hh:mm:ss", exactly as
 * gcc renders them: the month is three letters, the day is
 * space-padded to two, times are zero-padded. __STDC_HOSTED__ is 1
 * on any hosted implementation. the Makefile also greps the stamped
 * date out of the -E output and compares it with `date +%b %e %Y` */
#if __STDC_HOSTED__ != 1
#error "__STDC_HOSTED__ must be 1"
#endif
#if !defined(__DATE__) || !defined(__TIME__)
#error "__DATE__ and __TIME__ must be defined"
#endif

const char *build_date = __DATE__;
const char *build_time = __TIME__;

int main(void) {
  int ok = 1;
  /* "Mmm dd yyyy": three letters, space, day (space or digit, digit),
   * space, four digits, end */
  if (build_date[0] < 'A' || build_date[0] > 'Z' ||
      build_date[1] < 'a' || build_date[1] > 'z' ||
      build_date[2] < 'a' || build_date[2] > 'z' ||
      build_date[3] != ' ' ||
      !(build_date[4] == ' ' || (build_date[4] >= '0' && build_date[4] <= '9')) ||
      build_date[5] < '0' || build_date[5] > '9' ||
      build_date[6] != ' ')
    ok = 0;
  for (int i = 7; i <= 10; i++)
    if (build_date[i] < '0' || build_date[i] > '9')
      ok = 0;
  if (build_date[11] != '\0')
    ok = 0;
  /* "hh:mm:ss": digits, colon, digits, colon, digits, end */
  if (build_time[2] != ':' || build_time[5] != ':' || build_time[8] != '\0')
    ok = 0;
  for (int i = 0; i < 8; i++) {
    if (i == 2 || i == 5)
      continue;
    if (build_time[i] < '0' || build_time[i] > '9')
      ok = 0;
  }
  return ok ? 0 : 1;
}
