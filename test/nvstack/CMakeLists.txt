set(SRCS nvstack.c)
set(PROG nvstack)

nvart_set_sources_properties(C_Default ${SRCS})

# set_property(
#   SOURCE
#     ${SRCS}
#   PROPERTY LLVM_OPT_PASSES
#     "-load ${NVART_PASSES_DIR}/NVArt/LLVMNVArt.so -transform-stores"
# )

nvart_add_executable(${PROG} ${SRCS})

add_dependencies(${PROG} LLVMNVArt)

target_link_libraries(${PROG} nvart)

add_custom_target(
  run-${PROG}
  DEPENDS
    ${PROG}
  COMMENT
    "Running ${PROG} ..."
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG}
)
