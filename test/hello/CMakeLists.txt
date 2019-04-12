set(NORMAL hello)
set(INSBIN x-hello)

set(SRCS hello.c)
nvs_set_sources_properties(C_Default ${SRCS})

# --- Normal binary without instrumentation (ignoring HelloPasses) --- #

add_executable(${NORMAL} ${SRCS})
target_include_directories(${NORMAL} PUBLIC ${CMAKE_HOME_DIRECTORY}/include)

# --- HelloPasses instrumented binary --- #

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/Hello/HelloPasses.so -hello"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPasses.so -hello2"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPasses.so -bin2mul"
)

nvs_add_executable(${INSBIN} ${SRCS})
add_dependencies(${INSBIN} HelloPasses)

# --- Normal tests --- #

set(NORMAL_PATH ${CMAKE_CURRENT_BINARY_DIR}/${NORMAL})

add_test(NAME ${NORMAL}-1 COMMAND ${NORMAL} 8 9)

set_tests_properties(${NORMAL}-1 PROPERTIES WILL_FAIL ON)

# --- Instrumented tests --- #

set(INSBIN_PATH ${CMAKE_CURRENT_BINARY_DIR}/${INSBIN})

add_test(NAME ${INSBIN}-1 COMMAND ${INSBIN})
add_test(NAME ${INSBIN}-2 COMMAND ${INSBIN} 5)
add_test(NAME ${INSBIN}-3 COMMAND ${INSBIN} 8 9)
