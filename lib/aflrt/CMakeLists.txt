set(SRCS afl-llvm-rt.o.c)

# This library has to build into the fuzzed program, otherwise afl-fuzz would
# say "No instrumentation detected".
add_library(afl STATIC ${SRCS})

# Check afl-2.52b/llvm_mode/Makefile for compile flags, options, and definitions.

target_compile_features(afl PUBLIC c_std_99)
target_compile_options(
  afl PUBLIC -Wall -Wextra -funroll-loops -Wno-pointer-sign -masm=intel
)
target_compile_definitions(afl PUBLIC _FORTIFY_SOURCE=2)
target_include_directories(afl PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
