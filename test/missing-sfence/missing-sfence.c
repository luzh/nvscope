#include "headers.h"

#define MMAP_SIZE (4096)
#define OPEN_FLAG (O_CREAT | O_RDWR | O_SYNC)
#define OPEN_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

struct nvobj {
  int value;
  int valid;
};

static int case1(void *pmem) {
  struct nvobj *pobj = (struct nvobj *)pmem;

  pobj->value = 9;
  pobj->valid = 1;

  return 0;
}

static int check(void *pmem) {
  struct nvobj *pobj = (struct nvobj *)pmem;

  if (pobj->valid && pobj->value != 9) {
    printf("Consistency check failed!\n");
    return 1;
  }

  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: missing-sfence <file> <caseN | checkN>\n");
    return 1;
  }

  char *filename = argv[1];
  char *command = argv[2];

  typedef int (*casefunc)(void *);
  casefunc cases[] = {case1, case1};
  casefunc runcase = NULL;

  typedef int (*checkfunc)(void *);
  checkfunc checkers[] = {check, check};
  checkfunc runchecker = NULL;

  if (strncmp(command, "case", 4) == 0) {
    size_t caseid = command[4] - '1';
    if (sizeof(cases) / sizeof(cases[0]) <= caseid) {
      printf("Error: invalid case id %zu\n", caseid);
      return 1;
    }
    runcase = cases[caseid];
  } else if (strncmp(command, "check", 5) == 0) {
    size_t checkerid = command[5] - '1';
    if (sizeof(checkers) / sizeof(checkers[0]) <= checkerid) {
      printf("Error: invalid checker id %zu\n", checkerid);
      return 1;
    }
    runchecker = checkers[checkerid];
  } else {
    printf("Error: invalid command %s\n", command);
    return 1;
  }

  int fd = open(filename, OPEN_FLAG, OPEN_MODE);
  if (fd < 0) {
    printf("Error: open '%s' failed!\n", argv[1]);
    return 1;
  }

  if (strncmp(filename, "/dev/", 5) != 0) {
    if (fallocate(fd, 0, 0, MMAP_SIZE) < 0) {
      printf("Error: fallocate failed!\n");
      close(fd);
      return 1;
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
