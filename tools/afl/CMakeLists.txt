set(PROG afl-fuzz)
set(SRCS afl-fuzz.c)

add_executable(${PROG} ${SRCS})

# Check afl-2.52b/Makefile for compile flags, options, and definitions.

target_compile_features(${PROG} PUBLIC c_std_99)
target_compile_options(
  ${PROG} PUBLIC -Wall -funroll-loops -Wno-pointer-sign -masm=intel
)
target_compile_definitions(
  ${PROG} PUBLIC _FORTIFY_SOURCE=2 AFL_PATH="${CMAKE_CURRENT_BINARY_DIR}"
  BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}" DOC_PATH="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(${PROG} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
target_link_options(${PROG} PUBLIC -ldl)
