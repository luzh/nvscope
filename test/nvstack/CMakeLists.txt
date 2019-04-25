set(NORMAL nvstack)
set(NVXBIN x-nvstack)

set(SRCS nvstack.c)
nvx_set_sources_properties(C_Default ${SRCS})

# --- Normal binary without instrumentation (ignoring NVXProfiling) --- #

add_executable(${NORMAL} ${SRCS})
target_include_directories(${NORMAL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)

# --- NVXProfiling instrumented binary --- #

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVX_PASSES_DIR}/NVX/NVXProfiling.so -instrument"
)

nvx_add_executable(${NVXBIN} ${SRCS})
add_dependencies(${NVXBIN} NVXProfiling)
target_link_libraries(${NVXBIN} clang_rt.nvx)

# --- Non-NVScope tests --- #

# Note newer tests depend on older ones because the stackfile is stateful.

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-push1  COMMAND ${NORMAL_PATH} push stackfile)
add_test(NAME ${NORMAL}-show1  COMMAND ${NORMAL_PATH} show stackfile)
add_test(NAME ${NORMAL}-pop1   COMMAND ${NORMAL_PATH} pop stackfile)

set(NVXBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NVXBIN})

add_test(NAME ${NVXBIN}-push1  COMMAND ${NVXBIN_PATH} push stackfile)
add_test(NAME ${NVXBIN}-show1  COMMAND ${NVXBIN_PATH} show stackfile)
add_test(NAME ${NVXBIN}-pop1   COMMAND ${NVXBIN_PATH} pop stackfile)
add_test(NAME ${NVXBIN}-pop2   COMMAND ${NVXBIN_PATH} pop stackfile)
add_test(NAME ${NVXBIN}-show2  COMMAND ${NVXBIN_PATH} show stackfile)
add_test(NAME ${NVXBIN}-push3  COMMAND ${NVXBIN_PATH} push stackfile)
add_test(NAME ${NVXBIN}-push4  COMMAND ${NVXBIN_PATH} push stackfile)
add_test(NAME ${NVXBIN}-show3  COMMAND ${NVXBIN_PATH} show stackfile)
add_test(NAME ${NVXBIN}-check1 COMMAND ${NVXBIN_PATH} check stackfile)

# --- NVScope tests --- #

add_custom_target(
  scope-${NVXBIN}-push DEPENDS ${NVXBIN} COMMENT "Testing ${NVXBIN} (push) ..."
  DEPENDS nvscope
  COMMAND
  ${NVX_TOOLS_DIR}/nvscope --nvs-mainproc ${NVXBIN_PATH} push stackfile --nvs-recovery ${NVXBIN_PATH} check stackfile
)
