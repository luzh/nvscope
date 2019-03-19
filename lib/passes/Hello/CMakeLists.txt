add_llvm_loadable_module(HelloPass Hello.cpp)

nvs_set_pass_properties(HelloPass)

install(TARGETS HelloPass DESTINATION lib)
