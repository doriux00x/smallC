/* switch / case / default: dispatch, fallthrough, nesting, labels in
 * branches, break semantics */

int classify(int x) {
  switch (x) {
    case 0: return 100;
    case 1: return 101;
    case 2: return 102;
    default: return -1;
  }
}

int fallthrough(int x) {
  int r = 0;
  switch (x) {
    case 1: r += 10;
    case 2: r += 20; break;
    case 3: r += 30; break;
    default: r = 99;
  }
  return r;
}

int shared(int x) {
  switch (x) {
    case 1:
    case 2:
      return 7;
    default:
      return 8;
  }
}

int nested(int x) {
  switch (x) {
    case 1:
      switch (x * 10) {
        case 10: return 10;
        case 20: return 20;
        default: return 0;
      }
    case 2: return 2;
    default: return -2;
  }
}

int no_default(int x) {
  switch (x) {
    case 5: return 5;
    case 6: return 6;
  }
  return 0;
}

int expr_case(int x) {
  switch (x) {
    case 1 + 1: return 11;
    case 3 * 4: return 12;
    default: return 13;
  }
}

int loop_with_switch(void) {
  int total = 0;
  for (int i = 0; i < 4; i++) {
    switch (i) {
      case 0: continue;
      case 1: break;
      case 2: total += 10; break;
      default: total += 100;
    }
    total += 1;
  }
  return total;
}

int break_in_if(int x) {
  switch (x) {
    case 1:
      if (x > 0)
        break;
      return 5;
    case 2: return 6;
  }
  return 7;
}

int label_in_else(int x) {
  switch (x) {
    if (x < 0)
      case 1: return 1;
    else
      case 2: return 2;
  }
  return 3;
}

int switch_in_loop_body(void) {
  int s = 0;
  for (int i = 0; i < 5; i++)
    switch (i % 2) {
      case 0: s += 1; break;
      default: s += 2;
    }
  return s;
}

int empty_switch(void) {
  switch (0) {
  }
  return 4;
}

int main() {
  if (classify(0) != 100) return 1;
  if (classify(2) != 102) return 2;
  if (classify(9) != -1) return 3;
  if (fallthrough(1) != 30) return 4;
  if (fallthrough(2) != 20) return 5;
  if (fallthrough(3) != 30) return 6;
  if (fallthrough(7) != 99) return 7;
  if (shared(1) != 7) return 8;
  if (shared(2) != 7) return 9;
  if (shared(3) != 8) return 10;
  if (nested(1) != 10) return 11;
  if (nested(2) != 2) return 12;
  if (nested(3) != -2) return 13;
  if (no_default(5) != 5) return 14;
  if (no_default(7) != 0) return 15;
  if (expr_case(2) != 11) return 16;
  if (expr_case(12) != 12) return 17;
  if (expr_case(0) != 13) return 18;
  if (loop_with_switch() != 113) return 19;
  if (break_in_if(1) != 7) return 20;
  if (break_in_if(2) != 6) return 21;
  if (break_in_if(3) != 7) return 22;
  if (label_in_else(1) != 1) return 23;
  if (label_in_else(2) != 2) return 24;
  if (label_in_else(3) != 3) return 25;
  if (switch_in_loop_body() != 7) return 26;
  if (empty_switch() != 4) return 27;
  printf("runswitch ok\n");
  return 0;
}
