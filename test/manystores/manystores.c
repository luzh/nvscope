#include "cacheops.h"
#include "headers.h"

#define MMAP_SIZE (256 * 1024)
#define OPEN_FLAGS (O_CREAT | O_RDWR | O_SYNC)
#define OPEN_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

/**
 * This case should not be considered inconsistent. We use it for performance
 * evaluation.
 */
static int case1(void *pmem) {
  if (((uint64_t)pmem & 4095) != 0) {
    printf("Error: pmem %p is not 4K-aligned!\n", pmem);
    return 1;
  }
  uint64_t *p64 = (uint64_t *)pmem;
  uint64_t *cnt = (uint64_t *)pmem;

  for (uint64_t i = 1; i < MMAP_SIZE / 8; ++i) {
    p64[i] = i;
    clflushopt(&p64[i]);
    sfence();

    *cnt = i;
    clflushopt(cnt);
    sfence();
  }

  printf("Stored %u words to pmem!\n", MMAP_SIZE / 8);

  return 0;
}

static int check1(void *pmem) {
  if (((uint64_t)pmem & 4095) != 0) {
    printf("Error: pmem %p is not 4K-aligned!\n", pmem);
    return 1;
  }
  uint64_t *p64 = (uint64_t *)pmem;

  int inhole = 0, err = 0;
  for (size_t i = 1; i < MMAP_SIZE / 8; ++i) {
    if (p64[i] != i && p64[i] == 0) {
      inhole = 1;
    } else if (inhole && p64[i] != 0) {
      err = 1;
      break;
    }
  }

  if (err) printf("Error: inconsistent pmem data detected!\n");

  return err;
}

static int nocheck(void *pmem) { return (pmem == NULL); }

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <file> <caseN | checkN>\n", argv[0]);
    return 1;
  }

  char *filename = argv[1];
  char *command = argv[2];

  typedef int (*casefunc)(void *);
  casefunc cases[] = {case1, case1};
  casefunc runcase = NULL;

  typedef int (*checkfunc)(void *);
  checkfunc checkers[] = {check1, nocheck};
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
    printf("Error: open '%s' failed!\n", argv[1]);
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

  if (runcase) err = runcase(pmem);
  if (runchecker) err = runchecker(pmem);

  munmap(pmem, MMAP_SIZE);

  return err;
}
