cmake_minimum_required(VERSION 3.13)

# Add CMake modules for NVArt.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_HOME_DIRECTORY}/cmake")
include(NVArtUtils)

project(NVArt
  DESCRIPTION "Automated recovery testing tools for NVMM applications"
)

# Require LLVM and Clang for instrumentation.
find_package(LLVM REQUIRED CONFIG)

nvart_print("Found LLVM ${LLVM_PACKAGE_VERSION}")
nvart_print("Using LLVMConfig.cmake in: ${LLVM_DIR}")
nvart_print("LLVM_TOOLS_BINARY_DIR: ${LLVM_TOOLS_BINARY_DIR}")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
nvart_print("Build type is ${CMAKE_BUILD_TYPE}")

# Update C and C++ compilers.
set(CMAKE_C_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang)
nvart_print("Using C compiler: ${CMAKE_C_COMPILER}")

set(CMAKE_CXX_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang++)
nvart_print("Using C++ compiler: ${CMAKE_CXX_COMPILER}")

# Show details of the make flow.
set(CMAKE_VERBOSE_MAKEFILE ON)

# Add directories.
# add_subdirectory(lib)
# add_subdirectory(test)
# add_subdirectory(tools)
