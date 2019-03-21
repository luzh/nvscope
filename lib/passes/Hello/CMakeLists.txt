add_llvm_loadable_module(HelloPasses Hello.cpp)

nvs_set_pass_properties(HelloPasses)

install(TARGETS HelloPasses DESTINATION lib)
