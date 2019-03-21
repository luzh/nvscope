add_llvm_loadable_module(NVScopePasses NVScope.cpp)

nvs_set_pass_properties(NVScopePasses)

install(TARGETS NVScopePasses DESTINATION lib)
