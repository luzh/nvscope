set(PROG nvstack)
set(SRCS nvstack.c)

nvart_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVART_PASSES_DIR}/NVArt/LLVMNVArt.so -probes"
)

nvart_add_executable(${PROG} ${SRCS})

add_dependencies(${PROG} LLVMNVArt)

target_link_libraries(${PROG} nvart)

add_custom_target(
  push-${PROG}
  DEPENDS
    ${PROG}
  COMMENT
    "Running ${PROG} ..."
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ./nvfile push
)

add_custom_target(
  pop-${PROG}
  DEPENDS
    ${PROG}
  COMMENT
    "Running ${PROG} ..."
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ./nvfile pop
)

add_custom_target(
  show-${PROG}
  DEPENDS
    ${PROG}
  COMMENT
    "Running ${PROG} ..."
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ./nvfile show
)
