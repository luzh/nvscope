#include "headers.h"
#include "pprint.h"

#define CONST_PRIO 0  // constructor priority

/* Initialize NVArt run-time data structures. Runs before target's main(). */
__attribute__((constructor(CONST_PRIO))) void __nvart_init(void) {
  PPISTR("NVArt runtime initialized");
}

void condstore(uint64_t *ptr, uint64_t val) {
  srand(time(0));
  if (rand() & 1) {
    *ptr = val;
    PPISTR("Performing store %zu to %p", val, (void *)ptr);
  } else {
    PPISTR("Skipping store %zu to %p", val, (void *)ptr);
  }
}
