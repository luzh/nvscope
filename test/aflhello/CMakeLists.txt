set(SRCS aflhello.c)
set(PROG aflhello)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/AFL/LLVMAFL.so -aflcov"
)

nvs_add_executable(${PROG} ${SRCS})

add_dependencies(${PROG} LLVMAFL)

target_link_libraries(${PROG} afl)

add_custom_target(
  run-${PROG}
  DEPENDS
    ${PROG}
  COMMENT
    "Running aflhello (no fuzzing) ..."
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG}
)

set(AFL_FUZZ ${CMAKE_BINARY_DIR}/tools/afl/afl-fuzz)
add_custom_target(
  fuzz-${PROG}
  DEPENDS
    afl-fuzz ${PROG}
  COMMENT
    "Fuzzing (AFL) aflhello ..."
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/testcases testcases
  COMMAND
    ${AFL_FUZZ} -i testcases -o findings ${CMAKE_CURRENT_BINARY_DIR}/${PROG}
)
