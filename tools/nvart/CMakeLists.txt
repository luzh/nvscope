set(TOOL nvfuzz)
set(SRCS nvfuzz.c)

add_executable(${TOOL} ${SRCS})

# Check afl/CMakeLists.txt for compile flags, options, and definitions.

target_compile_features(${TOOL} PUBLIC c_std_99)
target_compile_options(
  ${TOOL} PUBLIC -Wall -funroll-loops -Wno-pointer-sign -masm=intel
)
target_compile_definitions(
  ${TOOL} PUBLIC _GNU_SOURCE PRINT_COLOR _FORTIFY_SOURCE=2
)
target_include_directories(${TOOL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
target_link_options(${TOOL} PUBLIC -ldl)

add_custom_target(
  run-${TOOL}
  DEPENDS
    ${TOOL}
  COMMENT
    "Running ${TOOL} ..."
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${TOOL}
)
