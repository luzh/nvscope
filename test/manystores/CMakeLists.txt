set(PROG manystores)
set(SRCS manystores.c)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/NVScope/NVScopePasses.so -probes"
)

nvs_add_executable(${PROG} ${SRCS})
add_dependencies(${PROG} NVScopePasses)
target_link_libraries(${PROG} nvsrt)

add_test(NAME ${PROG}-case1  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> casefile1 case1)
add_test(NAME ${PROG}-check1 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> casefile1 check1)
add_test(NAME ${PROG}-case2  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> casefile2 case2)
add_test(NAME ${PROG}-check2 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> casefile2 check2)

add_custom_target(
  scope-${PROG} DEPENDS ${PROG} COMMENT "Testing ${PROG} with nvscope ..."
  DEPENDS nvscope
  COMMAND
    ${NVS_TOOLS_DIR}/nvscope ${CMAKE_CURRENT_BINARY_DIR}/${PROG} pmemfile1 case1
)
