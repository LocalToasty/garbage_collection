#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc.h"

int main(int argc, char** argv) {
  (void)(argc);
  (void)(argv);

  for (size_t i = 0; i < ((size_t)1 << 20); i++) {
    int* foo = malloc(2 << 10);
    memset(foo, 0, 2 << 10);
    printf("%d %ld\n", *foo, i);
  }

  return 0;
}
