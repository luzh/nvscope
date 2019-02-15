set(SRCS afl-llvm-rt.o.c)

add_library(afl SHARED ${SRCS})

target_compile_features(afl PUBLIC c_std_99)
target_compile_options(
  afl PUBLIC -Wall -Wextra -funroll-loops -Wno-pointer-sign -masm=intel
)
target_compile_definitions(afl PUBLIC _FORTIFY_SOURCE=2)
target_include_directories(afl PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
