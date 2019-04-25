set(NORMAL aflhello)
set(AFLBIN x-aflhello)

set(SRCS aflhello.c)
nvx_set_sources_properties(C_Default ${SRCS})

# --- Normal binary without instrumentation (ignoring AFLPasses) --- #

add_executable(${NORMAL} ${SRCS})
target_include_directories(${NORMAL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)

# --- AFLPasses instrumented binary --- #

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVX_PASSES_DIR}/AFL/AFLPasses.so -aflcov"
)

nvx_add_executable(${AFLBIN} ${SRCS})
add_dependencies(${AFLBIN} AFLPasses)
target_link_libraries(${AFLBIN} aflrt)

# --- Normal tests --- #

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-exit0 COMMAND bash -c "${NORMAL_PATH} <<< 123")
add_test(NAME ${NORMAL}-abort COMMAND bash -c "${NORMAL_PATH} <<< abc")

set_tests_properties(${NORMAL}-abort PROPERTIES WILL_FAIL ON)

set(AFLBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${AFLBIN})

add_test(NAME ${AFLBIN}-exit0 COMMAND bash -c "${AFLBIN_PATH} <<< 123")
add_test(NAME ${AFLBIN}-abort COMMAND bash -c "${AFLBIN_PATH} <<< abc")

set_tests_properties(${AFLBIN}-abort PROPERTIES WILL_FAIL ON)

# --- AFL fuzzing tests --- #

set(AFL_FUZZ ${CMAKE_BINARY_DIR}/tools/afl/afl-fuzz)

add_custom_target(
  fuzz-${AFLBIN}
  DEPENDS
    afl-fuzz ${AFLBIN}
  COMMENT
    "Fuzzing (AFL) aflhello ..."
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/testcases testcases
  COMMAND
    ${AFL_FUZZ} -i testcases -o findings ${AFLBIN_PATH}
)
