nvart_set_sources_properties(C_Default aflhello.c)

set_property(
  SOURCE
    aflhello.c
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVART_PASSES_DIR}/AFL/LLVMAFL.so -aflcov"
)

nvart_add_executable(aflhello aflhello.c)

add_dependencies(aflhello LLVMAFL)

target_link_libraries(aflhello afl)

set(AFL_FUZZ ${CMAKE_BINARY_DIR}/tools/afl/afl-fuzz)
add_custom_target(
  fuzz-aflhello
  DEPENDS
    afl-fuzz aflhello
  COMMENT
    "Fuzzing (AFL) aflhello ..."
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/testcases testcases
  COMMAND
    ${AFL_FUZZ} -i testcases -o findings ${CMAKE_CURRENT_BINARY_DIR}/aflhello
)
