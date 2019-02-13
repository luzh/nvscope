cmake_minimum_required(VERSION 3.13)

project(NVArt
  DESCRIPTION "Automated recovery testing tools for NVMM applications"
)

# Add CMake modules for NVArt.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(NVArtUtils)

if(NOT LLVM_INSTALL_DIR)
  set(LLVM_INSTALL_DIR "${CMAKE_SOURCE_DIR}/llvm/lib/cmake/llvm")
endif()
if(NOT EXISTS ${LLVM_INSTALL_DIR})
  nvart_fatal("LLVM_INSTALL_DIR '${LLVM_INSTALL_DIR}' does not exist!")
endif()
list(APPEND CMAKE_PREFIX_PATH ${LLVM_INSTALL_DIR})

# Require LLVM and Clang for instrumentation.
find_package(LLVM REQUIRED CONFIG 7.0.1)

# Print LLVM information.
nvart_print("Found LLVM ${LLVM_PACKAGE_VERSION}")
nvart_print("LLVM build type: ${LLVM_BUILD_TYPE}")
nvart_print("Using LLVMConfig.cmake in: ${LLVM_CMAKE_DIR}")
nvart_print("LLVM_DEFINITIONS: ${LLVM_DEFINITIONS}")
nvart_print("LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
nvart_print("LLVM_TOOLS_BINARY_DIR: ${LLVM_TOOLS_BINARY_DIR}")

# Update C and C++ compilers.
set(CMAKE_C_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang)
nvart_print("Using C compiler: ${CMAKE_C_COMPILER}")
set(CMAKE_CXX_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang++)
nvart_print("Using C++ compiler: ${CMAKE_CXX_COMPILER}")

# Import LLVM's CMake modules.
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

# Set the project build type.
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
nvart_print("Project build type: ${CMAKE_BUILD_TYPE}")

# Show details of the make flow.
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_INSTALL_PREFIX /tmp/nvart)
# This is for test and tools to find the latest build of NVArt passes.
set(NVART_PASSES_DIR ${CMAKE_BINARY_DIR}/lib/passes)

nvart_print("CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

# Add directories.
add_subdirectory(lib)
add_subdirectory(test)
