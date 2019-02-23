#include "debug.h"
#include "headers.h"

#define MMAP_SIZE 4096
#define META_SIZE 1
#define MAX_VALUES 12

enum command { CMD_NONE, CMD_PUSH, CMD_POP, CMD_SHOW, CMD_CHECK };

int checkmeta(void *pmem) {
  uint64_t *nvals = (uint64_t *)pmem;

  if (*nvals > MAX_VALUES) {
    FATAL("Invalid stack value count!");
    return EINVAL;
  }

  return 0;
}

int push(void *pmem, uint64_t value) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE + nvals - 1;

  if (nvals == MAX_VALUES) {
    WARNF("Stack is full, ignoreing value 0x%lx", value);
    return 1;
  }

  *(top + 1) = value;
  _mm_clflushopt(top);
  _mm_sfence();

  *pnvals = nvals + 1;
  _mm_clflushopt(pnvals);
  _mm_sfence();

  ACTF("Pushed value 0x%lx into the stack!", value);

  return 0;
}

int pop(void *pmem, uint64_t *retval) {
  int err = checkmeta(pmem);
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

  ACTF("Poped value 0x%lx off the stack!", value);

  return 0;
}

int show(void *pmem) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *valptr = (uint64_t *)pmem + META_SIZE;

  if (nvals == 0)
    SAYF("Stack is empty.");
  else
    SAYF("Stack values (total %ld, top on the right):", nvals);

  for (uint64_t i = 0; i < nvals; i++) SAYF(" 0x%lx", valptr[i]);
  SAYF("\n");

  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) FATAL("Usage: nvstack <file> <push | pop | check>");

  char *nvfile = argv[1];
  enum command cmd = CMD_NONE;

  if (strcmp(argv[2], "push") == 0)
    cmd = CMD_PUSH;
  else if (strcmp(argv[2], "pop") == 0)
    cmd = CMD_POP;
  else if (strcmp(argv[2], "show") == 0)
    cmd = CMD_SHOW;
  else
    FATAL("Invalid command %s\n", argv[2]);

  int fd = open(nvfile, O_CREAT | O_RDWR | O_SYNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (fd < 0) FATAL("open '%s' failed!\n", argv[1]);

  void *pmem = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pmem == MAP_FAILED) {
    close(fd);
    FATAL("mmap failed!");
  }

  switch (cmd) {
    case CMD_PUSH:
      for (int i = 0; i < 3; i++) push(pmem, i);
      break;
    case CMD_POP:
      for (int i = 0; i < 2; i++) pop(pmem, NULL);
      break;
    case CMD_SHOW:
      show(pmem);
      break;
    default:
      break;
  }

  close(fd);
  munmap(pmem, MMAP_SIZE);

  return 0;
}
