# AFL fuzzing test programs

The program `afl-clang-fast` or `afl-clang-fast++` (not included in this repo)
wraps `clang` or `clang++` in the following manner.

```C
/path/to/clang -Xclang -load -Xclang /path/to/afl-llvm-pass.so \
-Qunused-arguments -O3 -funroll-loops -Wall -D_FORTIFY_SOURCE=2 -g \
-Wno-pointer-sign -DAFL_PATH="/path/to/afl" -DBIN_PATH="/path/to/afl" \
-DVERSION="2.52b" -DUSE_TRACE_PC -g -O3 -funroll-loops -D__AFL_COMPILER=1 \
-D__AFL_HAVE_MANUAL_CONTROL=1 -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION=1 \
-D__AFL_LOOP\(_A\)="({ static volatile char *_B __attribute__((used)); _B = \
(char*)\"##SIG_AFL_PERSISTENT##\"; __attribute__((visibility(\"default\"))) \
int _L(unsigned int) __asm__(\"__afl_persistent_loop\"); _L(_A); })" \
-D__AFL_INIT\(\)="do { static volatile char *_A __attribute__((used)); _A = \
(char*)\"##SIG_AFL_DEFER_FORKSRV##\"; __attribute__((visibility(\"default\"))) \
void _I(void) __asm__(\"__afl_manual_init\"); _I(); } while (0)" \
/path/to/afl-llvm-rt.o aflhello.c -o aflhello
```

The lengthy `__AFL_LOOP` and `__AFL_INIT` compiler definitions are equivalent to
define the following macros in the program to fuzz:

```C
#define __AFL_LOOP(_A) (\
{ \
  static volatile char *_B __attribute__((used)); \
  _B = (char*)"##SIG_AFL_PERSISTENT##"; \
  __attribute__((visibility("default"))) int _L(unsigned int) __asm__("__afl_persistent_loop"); \
  _L(_A); \
})

#define __AFL_INIT() \
do { \
  static volatile char *_A __attribute__((used)); \
  _A = (char*)"##SIG_AFL_DEFER_FORKSRV##"; \
  __attribute__((visibility("default"))) void _I(void) __asm__("__afl_manual_init"); \
  _I(); \
} while (0)
```

This repository does not include `afl-clang-fast` or `afl-clang-fast++`. When
these compiler definitions become necessary, we can add them in the CMake flow.
See more information in afl's `llvm_mode/README.llvm` and `docs`.
