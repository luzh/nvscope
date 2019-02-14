add_llvm_loadable_module(LLVMHola Hola.cpp)

nvart_set_pass_properties(LLVMHola)

install(TARGETS LLVMHola DESTINATION lib)
