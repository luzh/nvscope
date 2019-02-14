nvart_set_sources_properties(C_Default nvstack.c)

set_property(
  SOURCE
    nvstack.c
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVART_PASSES_DIR}/Hola/LLVMHola.so -hola"
)

nvart_add_executable(nvstack nvstack.c)

add_dependencies(nvstack LLVMHola)
