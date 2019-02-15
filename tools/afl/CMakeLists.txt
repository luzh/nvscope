set(SRCS afl-fuzz.c)

add_executable(afl-fuzz ${SRCS})

target_compile_features(afl-fuzz PUBLIC c_std_99)
target_compile_options(
  afl-fuzz PUBLIC -Wall -funroll-loops -Wno-pointer-sign -masm=intel
)
target_compile_definitions(
  afl-fuzz PUBLIC _FORTIFY_SOURCE=2 AFL_PATH="${CMAKE_CURRENT_BINARY_DIR}"
  BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}" DOC_PATH="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(afl-fuzz PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
target_link_options(afl-fuzz PUBLIC -ldl)
