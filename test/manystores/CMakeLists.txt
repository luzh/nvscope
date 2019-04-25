set(NORMAL manystores)
set(NVSBIN x-manystores)

set(SRCS manystores.c)
nvs_set_sources_properties(C_Default ${SRCS})

# --- Normal binary without instrumentation (ignoring NVScopePasses) --- #

add_executable(${NORMAL} ${SRCS})
target_include_directories(${NORMAL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)

# --- NVScopePasses instrumented binary --- #

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/NVScope/NVScopePasses.so -probes"
)

nvs_add_executable(${NVSBIN} ${SRCS})
add_dependencies(${NVSBIN} NVScopePasses)
target_link_libraries(${NVSBIN} clang_rt.nvx)

# --- Non-NVScope tests --- #

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-case1  COMMAND ${NORMAL_PATH} case1 casefile1)
add_test(NAME ${NORMAL}-check1 COMMAND ${NORMAL_PATH} check1 casefile1)

set(NVSBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NVSBIN})

add_test(NAME ${NVSBIN}-case1  COMMAND ${NVSBIN_PATH} case1 casefile1)
add_test(NAME ${NVSBIN}-check1 COMMAND ${NVSBIN_PATH} check1 casefile1)

add_test(NAME ${NVSBIN}-case2  COMMAND ${NVSBIN_PATH} case2 casefile2)
add_test(NAME ${NVSBIN}-check2 COMMAND ${NVSBIN_PATH} check2 casefile2)

# --- NVScope tests --- #

add_custom_target(
  scope-${NVSBIN}-1 DEPENDS ${NVSBIN} COMMENT "Testing ${NVSBIN} with nvscope ..."
  DEPENDS nvscope
  COMMAND
    ${NVS_TOOLS_DIR}/nvscope --nvs-mainproc ${NVSBIN_PATH} case1 pmemfile1 --nvs-recovery ${NVSBIN_PATH} check1 pmemfile1
)
