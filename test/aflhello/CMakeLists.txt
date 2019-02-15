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
