set(NORMAL nvstack)
set(NVSBIN nvstack-nvs)

set(SRCS nvstack.c)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/NVScope/NVScopePasses.so -probes"
)

add_executable(${NORMAL} ${SRCS})

nvs_add_executable(${NVSBIN} ${SRCS})
add_dependencies(${NVSBIN} NVScopePasses)
target_link_libraries(${NVSBIN} nvsrt)

# Note newer tests depend on older ones because the stackfile is stateful.

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-push1  COMMAND ${NORMAL_PATH} push stackfile)
add_test(NAME ${NORMAL}-show1  COMMAND ${NORMAL_PATH} show stackfile)
add_test(NAME ${NORMAL}-pop1   COMMAND ${NORMAL_PATH} pop stackfile)

set(NVSBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NVSBIN})

add_test(NAME ${NVSBIN}-push1  COMMAND ${NVSBIN_PATH} push stackfile)
add_test(NAME ${NVSBIN}-show1  COMMAND ${NVSBIN_PATH} show stackfile)
add_test(NAME ${NVSBIN}-pop1   COMMAND ${NVSBIN_PATH} pop stackfile)
add_test(NAME ${NVSBIN}-pop2   COMMAND ${NVSBIN_PATH} pop stackfile)
add_test(NAME ${NVSBIN}-show2  COMMAND ${NVSBIN_PATH} show stackfile)
add_test(NAME ${NVSBIN}-push3  COMMAND ${NVSBIN_PATH} push stackfile)
add_test(NAME ${NVSBIN}-push4  COMMAND ${NVSBIN_PATH} push stackfile)
add_test(NAME ${NVSBIN}-show3  COMMAND ${NVSBIN_PATH} show stackfile)
add_test(NAME ${NVSBIN}-check1 COMMAND ${NVSBIN_PATH} check stackfile)

add_custom_target(
  scope-${NVSBIN}-push DEPENDS ${NVSBIN} COMMENT "Testing ${NVSBIN} (push) ..."
  DEPENDS nvscope
  COMMAND
  ${NVS_TOOLS_DIR}/nvscope --nvs-mainproc ${NVSBIN_PATH} push stackfile --nvs-recovery ${NVSBIN_PATH} check stackfile
)
