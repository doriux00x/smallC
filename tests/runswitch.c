/* switch / case / default: dispatch, fallthrough, nesting, labels in
 * branches, break semantics, GNU case ranges */

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

  if (range_cat(0) != 40 || range_cat(1) != 10 || range_cat(2) != 10 ||
      range_cat(3) != 10 || range_cat(4) != 20 || range_cat(6) != 20 ||
      range_cat(7) != 40 || range_cat(8) != 30 || range_cat(99) != 40)
    return 28;
  if (range_edges(9) != 5) return 29;
  if (range_edges(100) != 5) return 30;
  if (range_edges(8) != 5) return 31;
  printf("runswitch ok\n");
  return 0;
}

/* GNU case ranges: 1 ... 3 matches any value in the closed span */
int range_cat(int x) {
  switch (x) {
    case 1 ... 3: return 10;
    case 4 ... 6: return 20;
    case 8: return 30;
    default: return 40;
  }
}

int range_edges(int x) {
  switch (x) {
    case 0 ... 9: return 5;
    case 100 ... 200: return 5;
    default: return 8;
  }
}
