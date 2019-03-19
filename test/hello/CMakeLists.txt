nvs_set_sources_properties(C_Default hello.c)

set_property(
  SOURCE
    hello.c
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -hello"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -hello2"
    "-load ${NVS_PASSES_DIR}/Hello/HelloPass.so -bin2mul"
)

nvs_add_executable(hello hello.c)

add_dependencies(hello HelloPass)
