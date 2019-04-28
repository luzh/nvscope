add_llvm_library(NVXProfiling MODULE NVXProfiling.cpp)

nvx_set_pass_properties(NVXProfiling)

install(TARGETS NVXProfiling DESTINATION lib)
