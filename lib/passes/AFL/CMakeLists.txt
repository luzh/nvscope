add_llvm_loadable_module(LLVMAFL AFL.cpp)

target_include_directories(
  LLVMAFL
  PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

install(TARGETS LLVMAFL DESTINATION lib)
