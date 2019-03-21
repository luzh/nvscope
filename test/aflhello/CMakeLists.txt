set(SRCS aflhello.c)
set(PROG aflhello)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/AFL/AFLPasses.so -aflcov"
)

nvs_add_executable(${PROG} ${SRCS})
add_dependencies(${PROG} AFLPasses)
target_link_libraries(${PROG} aflrt)

add_test(NAME ${PROG}-exit0 COMMAND bash -c "./${PROG} <<< 123")
add_test(NAME ${PROG}-abort COMMAND bash -c "./${PROG} <<< abc")
set_tests_properties(${PROG}-abort PROPERTIES WILL_FAIL ON)

set(AFL_FUZZ ${CMAKE_BINARY_DIR}/tools/afl/afl-fuzz)
add_custom_target(
  fuzz-${PROG}
  DEPENDS
    afl-fuzz ${PROG}
  COMMENT
    "Fuzzing (AFL) aflhello ..."
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/testcases testcases
  COMMAND
    ${AFL_FUZZ} -i testcases -o findings ${CMAKE_CURRENT_BINARY_DIR}/${PROG}
)
