#include "headers.h"
#include "cacheops.h"

#define MMAP_SIZE (4096)
#define OPEN_FLAGS (O_CREAT | O_RDWR | O_SYNC)
#define OPEN_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

#define META_SIZE (8)
#define MAX_VALUES (1024)

enum stackops { OP_NONE, OP_PUSH, OP_POP, OP_SHOW, OP_CHECK };

static int checkmeta(void *pmem) {
  uint64_t *nvals = (uint64_t *)pmem;

  if (*nvals > MAX_VALUES) {
    printf("Error: Invalid stack value count!\n");
    return EINVAL;
  }

  return 0;
}

static int check(void *pmem) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *valptr = (uint64_t *)pmem + META_SIZE / 8;

  for (uint64_t i = 0; i < nvals; i++) {
    if (valptr[i] != i + 1) {
      err += 1;
      break;
    }
  }

  if (err)
    printf("Error: Detected inconsistent stack data.\n");
  else
    printf("Stack data looks good.\n");

  return err;
}

static int peek(void *pmem, uint64_t *topval) {
  int err = checkmeta(pmem);
  if (err) return err;

  if (topval == NULL) return EINVAL;

  uint64_t nvals = *((uint64_t *)pmem);
  if (nvals == 0) *topval = 0;

  uint64_t *top = (uint64_t *)pmem + META_SIZE / 8 + nvals - 1;
  *topval = *top;

  return 0;
}

static int push(void *pmem, uint64_t value) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE / 8 + nvals - 1;

  if (nvals == MAX_VALUES) {
    printf("Stack is full, ignoring value %lu\n", value);
    return EINVAL;
  }

  *(top + 1) = value;
  clflushopt(top + 1);
  sfence();

  *pnvals = nvals + 1;
  clflushopt(pnvals);
  sfence();

  printf("Pushed value %lu into the stack.\n", value);

  return 0;
}

static int pop(void *pmem, uint64_t *retval) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *top = (uint64_t *)pmem + META_SIZE / 8 + nvals - 1;

  if (nvals == 0) {
    printf("Stack is empty, no value to pop.\n");
    return EINVAL;
  }

  uint64_t value = *top;
  if (retval != NULL) *retval = value;

  *pnvals = nvals - 1;
  clflushopt(pnvals);
  sfence();

  printf("Poped value %lu off the stack.\n", value);

  return 0;
}

static int show(void *pmem) {
  int err = checkmeta(pmem);
  if (err) return err;

  uint64_t *pnvals = (uint64_t *)pmem;
  uint64_t nvals = *pnvals;
  uint64_t *valptr = (uint64_t *)pmem + META_SIZE / 8;

  printf("Stack values (total %ld, top on the right):", nvals);
  for (uint64_t i = 0; i < nvals; i++) printf(" %lu", valptr[i]);
  printf("\n");

  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: nvstack <push | pop | show | check> <file>\n");
    return 1;
  }

  char *command = argv[1];
  char *filename = argv[2];

  enum stackops op = OP_NONE;
  if (strcmp(command, "push") == 0)
    op = OP_PUSH;
  else if (strcmp(command, "pop") == 0)
    op = OP_POP;
  else if (strcmp(command, "show") == 0)
    op = OP_SHOW;
  else if (strcmp(command, "check") == 0)
    op = OP_CHECK;
  else {
    printf("Error: invalid command %s\n", command);
    return 1;
  }

  int fd = open(filename, OPEN_FLAGS, OPEN_MODE);
  if (fd < 0) {
    printf("Error: open '%s' failed!\n", filename);
    return 1;
  }

  if (op == OP_PUSH || op == OP_POP) {
    if (strncmp(filename, "/dev/", 5) != 0) {
      if (fallocate(fd, 0, 0, MMAP_SIZE) < 0) {
        printf("Error: fallocate failed!\n");
        close(fd);
        return 1;
      }
    }
  }

  void *pmem = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pmem == MAP_FAILED) {
    close(fd);
    printf("Error: mmap failed!\n");
    return 1;
  }

  int err = 0;
  uint64_t topval;
  switch (op) {
    case OP_PUSH:
      if ((err = peek(pmem, &topval)) == 0) push(pmem, topval + 1);
      break;
    case OP_POP:
      pop(pmem, NULL);
      break;
    case OP_SHOW:
      show(pmem);
      break;
    case OP_CHECK:
      err = check(pmem);
      break;
    default:
      break;
  }

  close(fd);
  munmap(pmem, MMAP_SIZE);

  return err;
}
