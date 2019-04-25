add_llvm_loadable_module(NVXPasses NVScope.cpp)

nvx_set_pass_properties(NVXPasses)

install(TARGETS NVXPasses DESTINATION lib)
