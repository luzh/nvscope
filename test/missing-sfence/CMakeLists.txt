set(PROG missing-sfence)
set(SRCS missing-sfence.c)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/NVScope/NVScope.so -probes"
)

nvs_add_executable(${PROG} ${SRCS})

add_dependencies(${PROG} NVScope)

target_link_libraries(${PROG} nvsrt)

add_test(NAME ${PROG}-case1  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> casefile case1)
add_test(NAME ${PROG}-check1 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> casefile check1)
