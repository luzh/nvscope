#include "common.h"

void condstore(uint64_t *ptr, uint64_t val) {
  srand(time(0));
  if (rand() & 1) {
    *ptr = val;
    printf("Performing store %zu to %p\n", val, (void *)ptr);
  } else {
    printf("Skipping store %zu to %p\n", val, (void *)ptr);
  }
}
