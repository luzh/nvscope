nvs_set_sources_properties(C_Default hello.c)

set_property(
  SOURCE
    hello.c
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/Hola/LLVMHola.so -hola"
    "-load ${NVS_PASSES_DIR}/Hola/LLVMHola.so -hola2"
)

nvs_add_executable(hello hello.c)

add_dependencies(hello LLVMHola)
