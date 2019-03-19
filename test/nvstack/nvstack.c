#include "headers.h"

#define MMAP_SIZE (512 * 1024)
#define META_SIZE 1
#define MAX_VALUES (10 * 1000 * 1000)

enum command { CMD_NONE, CMD_PUSH, CMD_POP, CMD_SHOW, CMD_CHECK };

int checkmeta(void *pmem) {
  uint64_t *nvals = (uint64_t *)pmem;

  if (*nvals > MAX_VALUES) {
    printf("Error: Invalid stack value count!\n");
    return EINVAL;
  }

  return 0;
}

int check(void *pmem) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *valptr = (uint64_t *)pmem + META_SIZE;

  for (uint64_t i = 0; i < nvals; i++) {
    if (valptr[i] != i + 1) {
      err += 1;
      break;
    }
  }

  // if (err)
  //   printf("Error: Detected inconsistent stack data.\n");
  // else
  //   printf("Stack data looks good.\n");

  return err;
}

int peek(void *pmem, uint64_t *topval) {
  int err = checkmeta(pmem);
  if (err) return err;

  if (topval == NULL) return EINVAL;

  uint64_t nvals = *((uint64_t *)pmem);
  if (nvals == 0) *topval = 0;

  uint64_t *top = (uint64_t *)pmem + META_SIZE + nvals - 1;
  *topval = *top;

  return 0;
}

int push(void *pmem, uint64_t value) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE + nvals - 1;

  if (nvals == MAX_VALUES) {
    printf("Stack is full, ignoring value %lu\n", value);
    return EINVAL;
  }

  *(top + 1) = value;
  _mm_clflushopt(top);
  _mm_sfence();

  *pnvals = nvals + 1;
  _mm_clflushopt(pnvals);
  _mm_sfence();

  // printf("Pushed value %lu into the stack.\n", value);

  return 0;
}

int pop(void *pmem, uint64_t *retval) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE + nvals - 1;

  if (nvals == 0) {
    printf("Stack is empty, no value to pop.\n");
    return EINVAL;
  }

  uint64_t value = *top;
  if (retval != NULL) *retval = value;

  *pnvals = nvals - 1;
  _mm_clflushopt(pnvals);
  _mm_sfence();

  printf("Poped value %lu off the stack.\n", value);

  return 0;
}

int show(void *pmem) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *valptr = (uint64_t *)pmem + META_SIZE;

  printf("Stack values (total %ld, top on the right):", nvals);
  for (uint64_t i = 0; i < nvals; i++) printf(" %lu", valptr[i]);
  printf("\n");

  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: nvstack <file> <push | pop | show | check>\n");
    abort();
  }

  char *nvfile = argv[1];
  enum command cmd = CMD_NONE;

  if (strcmp(argv[2], "push") == 0)
    cmd = CMD_PUSH;
  else if (strcmp(argv[2], "pop") == 0)
    cmd = CMD_POP;
  else if (strcmp(argv[2], "show") == 0)
    cmd = CMD_SHOW;
  else if (strcmp(argv[2], "check") == 0)
    cmd = CMD_CHECK;
  else {
    printf("Error: invalid command %s\n", argv[2]);
    abort();
  }

  int fd = open(nvfile, O_CREAT | O_RDWR | O_SYNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (fd < 0) {
    printf("Error: open '%s' failed!\n", argv[1]);
    abort();
  }

  void *pmem = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pmem == MAP_FAILED) {
    close(fd);
    printf("Error: mmap failed!\n");
    abort();
  }

  int err = 0;
  uint64_t topval;
  switch (cmd) {
    case CMD_PUSH:
      if ((err = peek(pmem, &topval)) == 0) {
        for (int i = 0; i < 10000; i++) push(pmem, topval + 1 + i);
        // for (int i = 0; i < 1; i++) push(pmem, topval + 1 + i);
      }
      break;
    case CMD_POP:
      pop(pmem, NULL);
      break;
    case CMD_SHOW:
      show(pmem);
      break;
    case CMD_CHECK:
      err = check(pmem);
      break;
    default:
      break;
  }

  close(fd);
  munmap(pmem, MMAP_SIZE);

  return err;
}
