/* dump mode: typedef front-end coverage (casts, sizeof) */
typedef int I;
typedef unsigned U;
typedef char *S;
typedef I (*fp)(I);

I g;
U gu = 7;

int f(int x) {
  I a = (I)x;
  I b = sizeof(I) + sizeof(fp) + sizeof(gu);
  S s = "x";
  fp p = f;
  return a + b + g + p(0) + gu;
}