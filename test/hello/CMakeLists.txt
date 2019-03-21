set(PROG hello)
set(SRCS hello.c)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -hello"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -hello2"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -bin2mul"
)

nvs_add_executable(${PROG} ${SRCS})

add_dependencies(${PROG} HelloPass)

add_test(NAME ${PROG}-1 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}>)
add_test(NAME ${PROG}-2 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> 5)
add_test(NAME ${PROG}-3 COMMAND $<TARGET_NAME_IF_EXISTS:${PROG}> 8 9)
