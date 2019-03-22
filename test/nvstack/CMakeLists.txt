set(PROG nvstack)
set(SRCS nvstack.c)

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

# Note newer tests depend on older ones because the stackfile is stateful.

add_test(NAME ${PROG}-push1  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> push stackfile)
add_test(NAME ${PROG}-show1  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> show stackfile)
add_test(NAME ${PROG}-pop1   COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> pop stackfile)
add_test(NAME ${PROG}-pop2   COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> pop stackfile)
add_test(NAME ${PROG}-show2  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> show stackfile)
add_test(NAME ${PROG}-push3  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> push stackfile)
add_test(NAME ${PROG}-push4  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> push stackfile)
add_test(NAME ${PROG}-show3  COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> show stackfile)
add_test(NAME ${PROG}-check1 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> check stackfile)

add_custom_target(
  scope-${PROG}-push DEPENDS ${PROG} COMMENT "Testing ${PROG} (push) ..."
  DEPENDS nvscope
  COMMAND
    ${NVS_TOOLS_DIR}/nvscope ${CMAKE_CURRENT_BINARY_DIR}/${PROG} push ${STACKFILE}
)
