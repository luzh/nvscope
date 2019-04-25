add_llvm_loadable_module(NVXProfiling NVXProfiling.cpp)

nvx_set_pass_properties(NVXProfiling)

install(TARGETS NVXProfiling DESTINATION lib)
