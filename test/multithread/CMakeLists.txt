set(NORMAL multithread)
set(NVXBIN x-multithread)

set(SRCS multithread.c)
nvx_set_sources_properties(C_Default ${SRCS})

# --- Normal binary without instrumentation (ignoring NVXProfiling) --- #

add_executable(${NORMAL} ${SRCS})
target_include_directories(${NORMAL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
target_link_options(${NORMAL} PUBLIC -pthread)

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

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-case1 COMMAND ${NORMAL_PATH} case1 casefile1)

set(NVXBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NVXBIN})

add_test(NAME ${NVXBIN}-case1 COMMAND ${NVXBIN_PATH} case1 casefile1)

# --- NVScope tests --- #

function(add_nvscope_case)
  foreach(N ${ARGV})
    add_custom_target(scope-${NVXBIN}-case${N}
      DEPENDS
        ${NVXBIN} nvscope
      COMMAND
        ${NVX_TOOLS_DIR}/nvscope
          --nvs-mainproc ${NVXBIN_PATH} case${N} casefile
          --nvs-recovery ${NVXBIN_PATH} check${N} casefile
    )
  endforeach()
endfunction()

add_nvscope_case(1 2)
