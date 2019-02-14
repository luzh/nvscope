#include "common.h"

void condstore(uint64_t *ptr, uint64_t val) {
  srand(time(0));
  if (rand() & 1) {
    *ptr = val;
    printf("performed store %zu to %p\n", val, (void *)ptr);
  } else {
    printf("skipped store %zu to %p\n", val, (void *)ptr);
  }
}
