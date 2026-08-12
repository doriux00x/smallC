int main() {
  int i = 0;
again:
  i++;
  goto skip;
  i = 100;
skip:
  if (i < 3)
    goto again;
  return 0;
}