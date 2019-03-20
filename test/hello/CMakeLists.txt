nvs_set_sources_properties(C_Default hello.c)

set(TARGET hello)

set_property(
  SOURCE
    hello.c
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -hello"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -hello2"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -bin2mul"
)

nvs_add_executable(${TARGET} hello.c)

add_dependencies(${TARGET} HelloPass)

add_test(NAME ${TARGET}-1 COMMAND $<TARGET_NAME_IF_EXISTS:${TARGET}>)
add_test(NAME ${TARGET}-2 COMMAND $<TARGET_NAME_IF_EXISTS:${TARGET}> 5)
add_test(NAME ${TARGET}-3 COMMAND $<TARGET_NAME_IF_EXISTS:${TARGET}> 8 9)
