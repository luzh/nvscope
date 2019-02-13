add_llvm_loadable_module(LLVMHola Hola.cpp)

if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  # On non-Debug builds cmake automatically defines NDEBUG. Explicitly undefine
  # it to enable opt's -stats and -debug output.
  # See more details in: llvm/lib/cmake/llvm/HandleLLVMOptions.cmake
  target_compile_options(LLVMHola PRIVATE -UNDEBUG)
endif()

target_include_directories(LLVMHola PRIVATE ${LLVM_INCLUDE_DIRS})
