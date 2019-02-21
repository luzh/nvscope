#include "debug.h"
#include "headers.h"

#define MMAP_SIZE 4096
#define META_SIZE 1
#define MAX_VALUES 128

int check(void *pmem) {
  uint64_t *nvals = (uint64_t *)pmem;

  if (*nvals > MAX_VALUES) {
    FATAL("Invalid stack value count!");
    return EINVAL;
  }

  return 0;
}

int push(void *pmem, uint64_t value) {
  int err = check(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE + nvals - 1;

  if (nvals == MAX_VALUES) {
    WARNF("Stack is full, accepting no more values.");
    return 1;
  }

  *(top + 1) = value;
  _mm_clflush(top);
  // _mm_sfence();

  *pnvals = nvals + 1;
  _mm_clflush(pnvals);
  // _mm_sfence();

  ACTF("Pushed value 0x%lx into the stack!", value);

  return 0;
}

int pop(void *pmem, uint64_t *retval) {
  int err = check(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE + nvals - 1;

  if (nvals == 0) {
    WARNF("Stack is empty, no value to pop.");
    return 1;
  }

  uint64_t value = *top;
  if (retval != NULL) *retval = value;

  *pnvals = nvals - 1;
  _mm_clflush(pnvals);
  // _mm_sfence();

  ACTF("Poped value 0x%lx off the stack!", value);

  return 0;
}

int printvals(void *pmem) {
  int err = check(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *valptr = (uint64_t *)pmem + META_SIZE;

  if (nvals == 0) SAYF("Stack is empty.");

  SAYF("Stack values (total %ld, top on the right):", nvals);
  for (uint64_t i = 0; i < nvals; i++) SAYF(" 0x%lx", valptr[i]);
  SAYF("\n");

  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) FATAL("Usage: nvstack <file> <push | pop | check> <value>");

  char *file = argv[1];
  // char *act = argv[2];
  // char *value = argv[3];

  int fd = open(file, O_CREAT | O_RDWR | O_SYNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (fd < 0) FATAL("open '%s' failed!\n", argv[1]);

  void *pmem = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pmem == MAP_FAILED) {
    close(fd);
    FATAL("mmap failed!");
  }

  push(pmem, 1);
  push(pmem, 2);
  push(pmem, 3);
  push(pmem, 4);
  push(pmem, 5);

  pop(pmem, NULL);
  pop(pmem, NULL);

  printvals(pmem);

  close(fd);
  munmap(pmem, MMAP_SIZE);

  return 0;
}
