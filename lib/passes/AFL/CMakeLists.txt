add_llvm_loadable_module(AFLPasses AFL.cpp)

target_include_directories(
  AFLPasses
  PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

install(TARGETS AFLPasses DESTINATION lib)
