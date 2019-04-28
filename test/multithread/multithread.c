#include "cacheops.h"
#include "headers.h"

#define MMAP_SIZE (4096)
#define OPEN_FLAGS (O_CREAT | O_RDWR | O_SYNC)
#define OPEN_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

void *case1func1(void *arg) {
  uint64_t *pval = (uint64_t *)arg;
  *pval = 0xAA;
  clwb(pval);
  sfence();

  return NULL;
}

void *case1func2(void *arg) {
  uint64_t *pval = (uint64_t *)arg;
  *pval = 0xBB;
  clwb(pval);
  sfence();

  return NULL;
}

static int case1(void *pmem) {
  uint64_t *pval1 = (uint64_t *)pmem;
  uint64_t *pval2 = (uint64_t *)pmem + 10;
  *pval1 = 0;
  *pval2 = 0;
  clflush(pval1);
  clflush(pval2);

  pthread_t case1th1, case1th2;

  // The two threads make two store-writeback-sfence sequences separately.
  // If NVX checks at each sfence(), it may occasionally report dirty stores
  // depending on the instruction sequences of the two threads.
  pthread_create(&case1th1, NULL, case1func1, pval1);
  pthread_create(&case1th2, NULL, case1func2, pval2);

  pthread_join(case1th1, NULL);
  pthread_join(case1th2, NULL);

  return 0;
}

void *case2func1(void *pmem) {
  uint64_t *pval1 = (uint64_t *)pmem;
  *pval1 = 0xAA;
  clwb(pval1);
  sfence();

  return NULL;
}

void *case2func2(void *pmem) {
  volatile uint64_t *pval1 = (uint64_t *)pmem;
  uint64_t *pval2 = (uint64_t *)pmem + 10;

  while (*pval1 != 0xAA) {
  }

  *pval2 = 0xBB;
  clwb(pval2);
  sfence();

  return NULL;
}

static int case2(void *pmem) {
  uint64_t *pval1 = (uint64_t *)pmem;
  uint64_t *pval2 = (uint64_t *)pmem + 10;
  *pval2 = 0;
  clflush(pval2);
  *pval1 = 0;
  clflush(pval1);

  pthread_t case2th1, case2th2;

  // The two threads make two store-writeback-sfence sequences separately.
  // If NVX checks at each sfence(), it may occasionally report dirty stores
  // depending on the instruction sequences of the two threads.
  pthread_create(&case2th1, NULL, case2func1, pmem);
  pthread_create(&case2th2, NULL, case2func2, pmem);

  pthread_join(case2th1, NULL);
  pthread_join(case2th2, NULL);

  return 0;
}

static int check1(void *pmem) {
  if (pmem)
    return 0;

  return 1;
}

static int check2(void *pmem) {
  uint64_t *pval1 = (uint64_t *)pmem;
  uint64_t *pval2 = (uint64_t *)pmem + 10;

  if (*pval2 == 0xBB && *pval1 != 0xAA)
    return 1;

  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <caseN | checkN> <file>\n", argv[0]);
    return 1;
  }

  char *command = argv[1];
  char *filename = argv[2];

  typedef int (*casefunc)(void *);
  casefunc cases[] = {case1, case2};
  casefunc runcase = NULL;

  typedef int (*checkfunc)(void *);
  checkfunc checkers[] = {check1, check2};
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
