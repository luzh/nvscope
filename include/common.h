#ifndef _COMMON_H_
#define _COMMON_H_

#include <argp.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <immintrin.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef PRINT_COLOR
#define PCRST "\x1B[0m"
#define PCBLK "\x1B[1m\x1B[30m"
#define PCRED "\x1B[1m\x1B[31m"
#define PCGRN "\x1B[1m\x1B[32m"
#define PCYLW "\x1B[1m\x1B[33m"
#define PCBLU "\x1B[1m\x1B[34m"
#define PCMGT "\x1B[1m\x1B[35m"
#define PCCYN "\x1B[1m\x1B[36m"
#define PCWHT "\x1B[1m\x1B[37m"
#else
#define PCRST
#define PCBLK
#define PCRED
#define PCGRN
#define PCYLW
#define PCBLU
#define PCMGT
#define PCCYN
#define PCWHT
#endif

#define printerr(fmt, ...)                                                    \
  do {                                                                        \
    (errno) ? fprintf(stderr, PCRED "Error: " PCRST "%s, %d, %s(): %s, " fmt, \
                      __FILE__, __LINE__, __func__, strerror(errno),          \
                      ##__VA_ARGS__)                                          \
            : fprintf(stderr, PCRED "Error: " PCRST "%s, %d, %s(): " fmt,     \
                      __FILE__, __LINE__, __func__, ##__VA_ARGS__);           \
  } while (0)

#define printwarn(fmt, ...)                                                 \
  do {                                                                      \
    fprintf(stdout, PCYLW "Warning: " PCRST "%s, %d, %s(): " fmt, __FILE__, \
            __LINE__, __func__, ##__VA_ARGS__);                             \
  } while (0)

#define errout(fmt, ...)          \
  do {                            \
    printerr(fmt, ##__VA_ARGS__); \
    goto out;                     \
  } while (0)

#ifdef DEBUG
#define printdbg(fmt, ...)                                                    \
  do {                                                                        \
    fprintf(stdout, PCYLW "-> " PCRST "%s(): " fmt, __func__, ##__VA_ARGS__); \
  } while (0)
#else
#define printdbg(fmt, ...)
#endif

/* handy print functions */
#define val2str(x) #x
#define printd(x) printdbg("%s = %d\n", #x, (int)x)
#define printu(x) printdbg("%s = %u\n", #x, (unsigned int)x)
#define printlu(x) printdbg("%s = %lu\n", #x, (unsigned long)x)
#define printx(x) printdbg("%s = 0x%x\n", #x, (unsigned int)x)
#define printlx(x) printdbg("%s = 0x%lx\n", #x, (unsigned long)x)
#define printstr(x) printdbg("%s = %s\n", #x, x)
#define printdef(x) printdbg("%s = %s\n", #x, val2str(x))

#endif
