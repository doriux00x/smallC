/* switch statements the parser should handle (dump-only) */

int classify(int x) {
  switch (x) {
    case 0: return 100;
    case 1: return 101;
    default: return -1;
  }
}

int const_cases(int x) {
  switch (x * 2) {
    case 1 + 1: return 1;
    case 3 * 4: return 2;
  }
  return 3;
}

int labels_in_branches(int x) {
  switch (x) {
    if (x < 0)
      case 1: return 1;
    else
      case 2: return 2;
    default: return 3;
  }
}

int nested_switches(int x) {
  switch (x) {
    case 1:
      switch (x) {
        case 1: return 10;
        default: return 11;
      }
    case 2: return 2;
  }
  return 0;
}

int empty_bodies(int x) {
  switch (x) {
    case 1:
    case 2:
      ;
    default:
      ;
  }
  return 0;
}
