#include "cacheops.h"
#include "headers.h"

#define MMAP_SIZE (4096)
#define OPEN_FLAGS (O_CREAT | O_RDWR | O_SYNC)
#define OPEN_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

static int case1(void *pmem) {
  uint64_t *ptr = (uint64_t *)pmem;
  *ptr = 123;
  clwb(ptr);
  sfence();
  return 0;
}

static int case2(void *pmem) {
  uint64_t *ptr = (uint64_t *)pmem;
  *ptr = 123;
  /* Unflushed range (in-page hex offset): [000, 008). */
  sfence();
  return 0;
}

static int case3(void *pmem) {
  char *ptr = (char *)pmem;

  memcpy(ptr, ptr + 500, 100);
  clflushopt(ptr);
  /* Unflushed range (in-page hex offset): [040, 064). */

  memset(ptr + 100, 0x03, 100);
  clwb(ptr + 128);
  /* Unflushed range (in-page hex offset): [064, 080), [0c0, 0c8). */

  sfence();

  return 0;
}

static int case4(void *pmem) {
  char *ptr = (char *)pmem;

  memset(ptr + 500, 0x04, 500);
  for (int offset = 0; offset < 900; offset += 64) {
    if (offset == 256 || offset == 448 || offset == 640 || offset == 704)
      continue;
    clflushopt(ptr + offset);
  }
  /* Unflushed range (in-page hex offset): [1f4, 200), [280, 300), [3c0, 3e8) */

  sfence();

  return 0;
}

void callee5(uint64_t *value) {
  value[0] = 0xC;
  value[3] = 0xD;
}

static int case5(void *pmem) {
  callee5(pmem);

  char *ptr = (char *)pmem;

  clwb(ptr + 200);
  memset(ptr + 200, 0xE, 300);
  clwb(ptr + 256);

  /**
   * Unflushed range (in-page hex offset):
   * [0, 008), [018, 020), [0c8, 100), [140, 1f4)
   */

  sfence();

  return 0;
}

static int nocheck(void *pmem) {
  /* cases are for missing flushes so this does not really check anything */
  if (pmem)
    return 0;

  return 1;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <caseN | checkN> <file>\n", argv[0]);
    return 1;
  }

  char *command = argv[1];
  char *filename = argv[2];

  typedef int (*casefunc)(void *);
  casefunc cases[] = {case1, case2, case3, case4, case5};
  casefunc runcase = NULL;

  typedef int (*checkfunc)(void *);
  checkfunc checkers[] = {nocheck};
  checkfunc runchecker = NULL;

  if (strncmp(command, "case", 4) == 0) {
    size_t caseid = command[4] - '1';
    if (sizeof(cases) / sizeof(cases[0]) <= caseid) {
      printf("Error: invalid case id %zu\n", caseid + 1);
      return 1;
    }
    runcase = cases[caseid];
  } else if (strncmp(command, "check", 5) == 0) {
    size_t checkerid = command[5] - '1';
    if (sizeof(checkers) / sizeof(checkers[0]) <= checkerid) {
      printf("Error: invalid checker id %zu\n", checkerid + 1);
      return 1;
    }
    runchecker = checkers[checkerid];
  } else {
    printf("Error: invalid command %s\n", command);
    return 1;
  }

  int fd = open(filename, OPEN_FLAGS, OPEN_MODE);
  if (fd < 0) {
    printf("Error: open '%s' failed!\n", filename);
    return 1;
  }

  if (runcase) {
    /* A checker should not modify the file with fallocate(). */
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

  close(fd);

  int err = 0;

  if (runcase)
    err = runcase(pmem);
  if (runchecker)
    err = runchecker(pmem);

  munmap(pmem, MMAP_SIZE);

  return err;
}
