set(TOOL afl-fuzz)
set(SRCS afl-fuzz.c)

add_executable(${TOOL} ${SRCS})

# Check afl-2.52b/Makefile for compile flags, options, and definitions.

target_compile_features(${TOOL} PUBLIC c_std_99)
target_compile_options(
  ${TOOL} PUBLIC -Wall -funroll-loops -Wno-pointer-sign -masm=intel
)
target_compile_definitions(
  ${TOOL} PUBLIC _FORTIFY_SOURCE=2 AFL_PATH="${CMAKE_CURRENT_BINARY_DIR}"
  BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}" DOC_PATH="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(${TOOL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
target_link_options(${TOOL} PUBLIC -ldl)
