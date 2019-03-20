cmake_minimum_required(VERSION 3.13)

project(NVScope
  DESCRIPTION "Analytics and testing tools for NVMM applications"
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add CMake modules for NVScope.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(nvscope)

if(NOT LLVM_INSTALL_DIR)
  set(LLVM_INSTALL_DIR "${CMAKE_SOURCE_DIR}/llvm/lib/cmake/llvm")
endif()
if(NOT EXISTS ${LLVM_INSTALL_DIR})
  nvs_fatal("LLVM_INSTALL_DIR '${LLVM_INSTALL_DIR}' does not exist!")
endif()
list(APPEND CMAKE_PREFIX_PATH ${LLVM_INSTALL_DIR})

# Require LLVM and Clang for instrumentation.
find_package(LLVM REQUIRED CONFIG 7.0.1)

# Print LLVM information.
nvs_print("Found LLVM ${LLVM_PACKAGE_VERSION}")
nvs_print("LLVM build type: ${LLVM_BUILD_TYPE}")
nvs_print("Using LLVMConfig.cmake in: ${LLVM_CMAKE_DIR}")
nvs_print("LLVM_DEFINITIONS: ${LLVM_DEFINITIONS}")
nvs_print("LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
nvs_print("LLVM_TOOLS_BINARY_DIR: ${LLVM_TOOLS_BINARY_DIR}")

# Update C and C++ compilers.
set(CMAKE_C_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang)
nvs_print("Using C compiler: ${CMAKE_C_COMPILER}")
set(CMAKE_CXX_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang++)
nvs_print("Using C++ compiler: ${CMAKE_CXX_COMPILER}")

# Import LLVM's CMake modules.
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

# Set the project build type.
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
nvs_print("Project build type: ${CMAKE_BUILD_TYPE}")

# Show details of the make flow. Or use make VERBOSE=1 for the same purpose.
# set(CMAKE_VERBOSE_MAKEFILE ON)

# Set install prefix.
set(CMAKE_INSTALL_PREFIX /tmp/nvscope)
nvs_print("CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

# This is for test and tools to find the latest build of NVScope passes.
set(NVS_PASSES_DIR ${CMAKE_BINARY_DIR}/lib/passes)
set(NVS_TOOLS_DIR ${CMAKE_BINARY_DIR}/tools/nvscope)

# Add directories.
add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(tools)
