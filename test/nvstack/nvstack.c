#include "headers.h"
#include "pprint.h"

#define BUFSIZE 100

void push(uint64_t *top, uint64_t value) {
  *top = value;
  PPWORK("Push value 0x%lx into the stack!", value);
  _mm_sfence();
}

int main(int argc, char **argv) {
  char *buf = malloc(BUFSIZE);

  memset(buf, 0xAC, BUFSIZE);
  _mm_clflush(buf);
  _mm_sfence();

  unsigned idx = 0;
  if (argc > 1) {
    idx = atoi(argv[1]);
    PPWORK("User-specified index is %u", idx);
  }

  idx = idx + 10;  // to be transformed by the NVArt pass
  PPWORK("Transformed index is (idx + 10) %u", idx);

  uint64_t *bufptr = (uint64_t *)(buf + idx);

  push(bufptr, 0x35);

  free(buf);

  return 0;
}
