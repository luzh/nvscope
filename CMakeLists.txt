cmake_minimum_required(VERSION 3.13)

project(NVX
  DESCRIPTION "LLVM Extension for Debugging NVMM Applications"
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable testing
include(CTest)

# Add CMake modules for NVX.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(nvx)

if(NOT LLVM_INSTALL_DIR)
  set(LLVM_INSTALL_DIR "${CMAKE_SOURCE_DIR}/llvm/lib/cmake/llvm")
endif()
if(NOT EXISTS ${LLVM_INSTALL_DIR})
  nvx_fatal("LLVM_INSTALL_DIR '${LLVM_INSTALL_DIR}' does not exist!")
endif()
list(APPEND CMAKE_PREFIX_PATH ${LLVM_INSTALL_DIR})

# Require LLVM and Clang for instrumentation.
find_package(LLVM REQUIRED CONFIG 7.0.1)

# Print LLVM information.
nvx_print("Found LLVM ${LLVM_PACKAGE_VERSION}")
nvx_print("LLVM build type: ${LLVM_BUILD_TYPE}")
nvx_print("Using LLVMConfig.cmake in: ${LLVM_CMAKE_DIR}")
nvx_print("LLVM_DEFINITIONS: ${LLVM_DEFINITIONS}")
nvx_print("LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
nvx_print("LLVM_TOOLS_BINARY_DIR: ${LLVM_TOOLS_BINARY_DIR}")

# Update C and C++ compilers.
set(CMAKE_C_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang)
nvx_print("Using C compiler: ${CMAKE_C_COMPILER}")
set(CMAKE_CXX_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang++)
nvx_print("Using C++ compiler: ${CMAKE_CXX_COMPILER}")

# Import LLVM's CMake modules.
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

# Set the project build type.
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
nvx_print("Project build type: ${CMAKE_BUILD_TYPE}")

# Show details of the make flow. Or use make VERBOSE=1 for the same purpose.
# set(CMAKE_VERBOSE_MAKEFILE ON)

# Set install prefix.
set(CMAKE_INSTALL_PREFIX /tmp/nvx
nvx_print("CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

# This is for test and tools to find the latest build of NVX passes.
set(NVX_PASSES_DIR ${CMAKE_BINARY_DIR}/lib/passes)
set(NVX_TOOLS_DIR ${CMAKE_BINARY_DIR}/tools/nvx)

# Options
option(COLORPRINT "Print messages with color" ON)
option(NVX_DEBUG "Print NVX debug messages" OFF)
if (NVX_DEBUG)
  nvx_warning("NVX_DEBUG set, may impact performance")
endif()

# Add directories.
add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(tools)
