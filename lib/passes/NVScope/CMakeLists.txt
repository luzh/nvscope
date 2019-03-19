add_llvm_loadable_module(NVScope NVScope.cpp)

nvs_set_pass_properties(NVScope)

install(TARGETS NVScope DESTINATION lib)
