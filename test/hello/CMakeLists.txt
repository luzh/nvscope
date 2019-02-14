nvart_set_sources_properties(C_Default hello.c)

set_property(
  SOURCE
    hello.c
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVART_PASSES_DIR}/Hola/LLVMHola.so -hola"
    "-load ${NVART_PASSES_DIR}/Hola/LLVMHola.so -hola2"
)

nvart_add_executable(hello hello.c)

add_dependencies(hello LLVMHola)
