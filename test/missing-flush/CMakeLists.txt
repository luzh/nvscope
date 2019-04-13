set(NORMAL missing-flush)
set(NVSBIN x-missing-flush)

set(SRCS missing-flush.c)
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
target_link_libraries(${NVSBIN} nvsrt)

# --- Non-NVScope tests --- #

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-case1 COMMAND ${NORMAL_PATH} case1 casefile1)

set(NVSBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NVSBIN})

add_test(NAME ${NVSBIN}-case1 COMMAND ${NVSBIN_PATH} case1 casefile1)

# --- NVScope tests --- #

function(add_nvscope_case)
  foreach(N ${ARGV})
    add_custom_target(scope-${NVSBIN}-case${N}
      DEPENDS
        ${NVSBIN} nvscope
      COMMAND
        ${NVS_TOOLS_DIR}/nvscope
          --nvs-mainproc ${NVSBIN_PATH} case${N} casefile
          --nvs-recovery ${NVSBIN_PATH} check1 casefile
    )
  endforeach()
endfunction()

add_nvscope_case(1 2 3 4 5)
