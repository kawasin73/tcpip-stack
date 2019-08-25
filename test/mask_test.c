#include <stdint.h>
#include <stdio.h>
#include "util.h"

#define MASK_SIZE 4

int main(int argc, char const *argv[]) {
  uint32_t mask[MASK_SIZE];

  fprintf(stderr, ">>> clr all <<<\n");
  maskclr(mask, MASK_SIZE);
  maskdbg(mask, MASK_SIZE);

  if (maskchk(mask, MASK_SIZE, 0, 1)) {
    fprintf(stderr, "check failed\n");
  }
  fprintf(stderr, ">>> set 31-65 <<<\n");
  maskset(mask, MASK_SIZE, 31, 34);
  maskdbg(mask, MASK_SIZE);

  if (maskchk(mask, MASK_SIZE, 30, 3)) {
    fprintf(stderr, "check failed : 1\n");
  }
  if (maskchk(mask, MASK_SIZE, 31, 35)) {
    fprintf(stderr, "check failed : 2\n");
  }
  if (!maskchk(mask, MASK_SIZE, 31, 34)) {
    fprintf(stderr, "check failed : 3\n");
  }
  if (!maskchk(mask, MASK_SIZE, 32, 2)) {
    fprintf(stderr, "check failed : 4\n");
  }

  fprintf(stderr, ">>> set all <<<\n");
  maskset(mask, MASK_SIZE, 0, MASK_SIZE * 32);
  maskdbg(mask, MASK_SIZE);

  if (!maskchk(mask, MASK_SIZE, 0, 1)) {
    fprintf(stderr, "check failed : first\n");
  }
  if (!maskchk(mask, MASK_SIZE, 0, MASK_SIZE * 32)) {
    fprintf(stderr, "check failed : all\n");
  }
  return 0;
}
