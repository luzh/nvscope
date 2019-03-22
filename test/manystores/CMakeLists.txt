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

set(RUNPROG ${CMAKE_CURRENT_BINARY_DIR}/${PROG})

add_test(NAME ${PROG}-case1  COMMAND ${RUNPROG} case1 casefile1)
add_test(NAME ${PROG}-check1 COMMAND ${RUNPROG} check1 casefile1)

add_test(NAME ${PROG}-case2  COMMAND ${RUNPROG} case2 casefile2)
add_test(NAME ${PROG}-check2 COMMAND ${RUNPROG} check2 casefile2)

add_custom_target(
  scope-${PROG}-1 DEPENDS ${PROG} COMMENT "Testing ${PROG} with nvscope ..."
  DEPENDS nvscope
  COMMAND
    ${NVS_TOOLS_DIR}/nvscope --nvs-mainproc ${RUNPROG} case1 pmemfile1 --nvs-recovery ${RUNPROG} check1 pmemfile1
)
