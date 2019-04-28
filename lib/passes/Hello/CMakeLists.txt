add_llvm_library(HelloPasses MODULE Hello.cpp)

nvx_set_pass_properties(HelloPasses)

install(TARGETS HelloPasses DESTINATION lib)
