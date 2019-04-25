set(NORMAL missing-sfence)
set(NVXBIN x-missing-sfence)

set(SRCS missing-sfence.c)
nvx_set_sources_properties(C_Default ${SRCS})

# --- Normal binary without instrumentation (ignoring NVXProfiling) --- #

add_executable(${NORMAL} ${SRCS})
target_include_directories(${NORMAL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)

# --- NVXProfiling instrumented binary --- #

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVX_PASSES_DIR}/NVX/NVXProfiling.so -probes"
)

nvx_add_executable(${NVXBIN} ${SRCS})
add_dependencies(${NVXBIN} NVXProfiling)
target_link_libraries(${NVXBIN} clang_rt.nvx)

# --- Non-NVScope tests --- #

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-case1  COMMAND ${NORMAL_PATH} case1 casefile1)
add_test(NAME ${NORMAL}-check1 COMMAND ${NORMAL_PATH} check1 casefile1)

set(NVXBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NVXBIN})

add_test(NAME ${NVXBIN}-case1  COMMAND ${NVXBIN_PATH} case1 casefile1)
add_test(NAME ${NVXBIN}-check1 COMMAND ${NVXBIN_PATH} check1 casefile1)

# --- NVScope tests --- #

# To be added
