#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

int main(int argc, char const *argv[]) {
  int failed = 0;
  void *data;
  size_t size;
  uint64_t data1 = (uint64_t)random();
  uint16_t data2 = (uint16_t)random();
  uint8_t data3 = (uint8_t)random();

  struct queue_head queue = {};

  if (queue_pop(&queue, &data, &size) != -1) {
    fprintf(stderr, "check failed : pop from empty queue\n");
    failed++;
  }

  if (queue_push(&queue, &data1, sizeof(uint64_t)) != 0) {
    fprintf(stderr, "check failed : push 1\n");
    failed++;
  }

  if (queue_push(&queue, &data2, sizeof(uint16_t)) != 0) {
    fprintf(stderr, "check failed : push 2\n");
    failed++;
  }

  if (queue_pop(&queue, &data, &size) != 0) {
    fprintf(stderr, "check failed : pop data1\n");
    failed++;
  } else if (size != sizeof(uint64_t)) {
    fprintf(stderr, "check failed : poped data1 size\n");
    failed++;
  } else if (*(uint64_t *)data != data1) {
    fprintf(stderr, "check failed : poped data1\n");
    failed++;
  }

  if (queue_push(&queue, &data3, sizeof(uint8_t)) != 0) {
    fprintf(stderr, "check failed : push 3\n");
    failed++;
  }

  if (queue_pop(&queue, &data, &size) != 0) {
    fprintf(stderr, "check failed : pop data2\n");
    failed++;
  } else if (size != sizeof(uint16_t)) {
    fprintf(stderr, "check failed : poped data2 size\n");
    failed++;
  } else if (*(uint16_t *)data != data2) {
    fprintf(stderr, "check failed : poped data2\n");
    failed++;
  }

  if (queue_pop(&queue, &data, &size) != 0) {
    fprintf(stderr, "check failed : pop data3\n");
    failed++;
  } else if (size != sizeof(uint8_t)) {
    fprintf(stderr, "check failed : poped data3 size\n");
    failed++;
  } else if (*(uint8_t *)data != data3) {
    fprintf(stderr, "check failed : poped data3\n");
    failed++;
  }

  if (queue_pop(&queue, &data, &size) != -1) {
    fprintf(stderr, "check failed : pop from all poped empty queue\n");
    failed++;
  }

  if (!failed) {
    fprintf(stderr, "TEST SUCCESS!\n");
    return 0;
  } else {
    fprintf(stderr, "TEST FAILED : %d errors\n", failed);
    return 1;
  }
}
