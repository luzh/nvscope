add_llvm_loadable_module(LLVMNVArt NVArt.cpp)

nvart_set_pass_properties(LLVMNVArt)

install(TARGETS LLVMNVArt DESTINATION lib)
