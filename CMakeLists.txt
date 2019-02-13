cmake_minimum_required(VERSION 3.13)

project(NVArt
  DESCRIPTION "Automated recovery testing tools for NVMM applications"
)

# Add CMake modules for NVArt.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_HOME_DIRECTORY}/cmake")
include(NVArtUtils)

if(NOT LLVM_DIR)
  set(LLVM_DIR "${CMAKE_HOME_DIRECTORY}/llvm")
endif()
if(NOT EXISTS ${LLVM_DIR})
  nvart_fatal("LLVM_DIR '${LLVM_DIR}' does not exist!")
endif()
list(APPEND CMAKE_PREFIX_PATH "${LLVM_DIR}")

# Require LLVM and Clang for instrumentation.
find_package(LLVM REQUIRED CONFIG 7.0.0)

nvart_print("Found LLVM ${LLVM_PACKAGE_VERSION}")
nvart_print("LLVM build type is ${LLVM_BUILD_TYPE}")
nvart_print("Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Update C and C++ compilers.
set(CMAKE_C_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang)
nvart_print("Using C compiler: ${CMAKE_C_COMPILER}")

set(CMAKE_CXX_COMPILER ${LLVM_TOOLS_BINARY_DIR}/clang++)
nvart_print("Using C++ compiler: ${CMAKE_CXX_COMPILER}")

nvart_print("LLVM_CMAKE_DIR: ${LLVM_CMAKE_DIR}")
nvart_print("LLVM_DEFINITIONS: ${LLVM_DEFINITIONS}")
nvart_print("LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
nvart_print("LLVM_TOOLS_BINARY_DIR: ${LLVM_TOOLS_BINARY_DIR}")

# list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
# include(HandleLLVMOptions) # load additional config
# include(AddLLVM) # used to add our own modules

# Propagate LLVM-specific variables to this project.
# add_definitions(${LLVM_DEFINITIONS})
# include_directories(${LLVM_INCLUDE_DIRS})

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
nvart_print("Project build type is ${CMAKE_BUILD_TYPE}")

# Show details of the make flow.
set(CMAKE_VERBOSE_MAKEFILE ON)

# Add directories.
# add_subdirectory(lib)
# add_subdirectory(test)
# add_subdirectory(tools)
