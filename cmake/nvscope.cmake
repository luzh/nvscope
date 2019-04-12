cmake_minimum_required(VERSION 3.13)

if(NOT WIN32)
  string(ASCII 27 Esc)
  set(ColorReset  "${Esc}[m")
  set(ColorBold   "${Esc}[1m")
  set(Red         "${Esc}[31m")
  set(Green       "${Esc}[32m")
  set(Yellow      "${Esc}[33m")
  set(Blue        "${Esc}[34m")
  set(Magenta     "${Esc}[35m")
  set(Cyan        "${Esc}[36m")
  set(White       "${Esc}[37m")
  set(BoldRed     "${Esc}[1;31m")
  set(BoldGreen   "${Esc}[1;32m")
  set(BoldYellow  "${Esc}[1;33m")
  set(BoldBlue    "${Esc}[1;34m")
  set(BoldMagenta "${Esc}[1;35m")
  set(BoldCyan    "${Esc}[1;36m")
  set(BoldWhite   "${Esc}[1;37m")
endif()

function(nvs_print MSGSTR)
  message(STATUS "${Blue}[NVS-INFO]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvs_warning MSGSTR)
  message(STATUS "${Yellow}[NVS-Warning]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvs_debug MSGSTR)
  message(STATUS "${Magenta}[NVS-Debug]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvs_fatal MSGSTR)
  message(FATAL_ERROR "${BoldRed}[NVS-Fatal]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvs_set_pass_properties PASS_TARGET)
# if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
#   # On non-Debug builds cmake automatically defines NDEBUG. Explicitly
#   # undefine it to enable opt's -stats and -debug output. See more details
#   # in: llvm/lib/cmake/llvm/HandleLLVMOptions.cmake
#   target_compile_options(${PASS_TARGET} PRIVATE -UNDEBUG)
# endif()

  target_include_directories(${PASS_TARGET} PRIVATE ${LLVM_INCLUDE_DIRS})
endfunction()

# Properties set by this function apply to both normal and instrumented targets.
function(nvs_set_sources_properties PROFILE)
# set(options)
# set(oneValueArgs PROFILE)
# set(multiValueArgs FILES)
# cmake_parse_arguments(
#   NVS_SOURCE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Our target application will be C sources.
  set(VALID_PROFILES "C_Default")
  if(NOT PROFILE IN_LIST VALID_PROFILES)
    nvs_fatal("Invalid source profile: ${PROFILE}")
  endif()

  set(SRCS ${ARGN})

  nvs_print("Using ${PROFILE} properties for ${SRCS}")

  set_property(
    SOURCE
      ${SRCS}
    PROPERTY COMPILE_DEFINITIONS
      _GNU_SOURCE MESSAGES_TO_STDOUT
  )

  set_property(
    SOURCE
      ${SRCS}
    PROPERTY INCLUDE_DIRECTORIES
      ${CMAKE_SOURCE_DIR}/include
  )

  # Specify -march for clflushopt/clwb to compile.
  # Skylake server processors (-march=skx) support both clflushopt and clwb.
  # Compile for a different architecture can raise run-time errors if the
  # executing maching does not have corresponding instructions.
  # See llvm/lib/Target/X86/X86.td for supported march options.
  set_property(
    SOURCE
      ${SRCS}
    PROPERTY COMPILE_OPTIONS
      -Wall -Wextra -march=native
  )
endfunction()

# Emulates CMake's default add_executable() and applies multiple LLVM passes.
function(nvs_add_executable)
  list(LENGTH ARGV ARGS_LEN)
  if(ARGS_LEN LESS "2")
    nvs_fatal("Required arguments: <target> <source1> [source2...]")
  endif()

  list(GET ARGV 0 EXE_TARGET)
  math(EXPR SRCS_LEN "${ARGS_LEN} - 1")
  list(SUBLIST ARGV 1 ${SRCS_LEN} SRC_NAMES)

  nvs_print("Sources for target ${EXE_TARGET}: ${SRC_NAMES}")

  set(C_SRC_EXTS "H;C")
  set(CXX_SRC_EXTS "HPP;CC;CPP")
  set(SRC_HEADER_EXTS "H;HPP")

  set(LINKER_LANG "")
  set(LLVM_BC_FILES "")

  foreach(SRC_NAME ${SRC_NAMES})
    string(REGEX MATCHALL "\.([a-z]+)$" SRC_NAME_EXT ${SRC_NAME})
    string(TOUPPER ${CMAKE_MATCH_1} SRC_NAME_EXT) # last file extension
    if(SRC_NAME_EXT IN_LIST CXX_SRC_EXTS)
      separate_arguments(DEBUG_OPTS UNIX_COMMAND ${CMAKE_CXX_FLAGS_DEBUG})
      separate_arguments(RELEASE_OPTS UNIX_COMMAND ${CMAKE_CXX_FLAGS_RELEASE})
      nvs_warning("Need to set proper C++ compile flags!")
      set(LINKER_LANG "CXX")
    elseif(SRC_NAME_EXT IN_LIST C_SRC_EXTS)
      separate_arguments(DEBUG_OPTS UNIX_COMMAND ${CMAKE_C_FLAGS_DEBUG})
      separate_arguments(RELEASE_OPTS UNIX_COMMAND ${CMAKE_C_FLAGS_RELEASE})
      list(APPEND DEBUG_OPTS "-std=gnu99")
      list(APPEND RELEASE_OPTS "-std=gnu99")
      if(NOT LINKER_LANG STREQUAL "CXX")
        set(LINKER_LANG "C")
      endif()
    else()
      nvs_fatal("Unsupported source type: ${SRC_NAME}")
    endif()

    if(SRC_NAME_EXT IN_LIST SRC_HEADER_EXTS)
      continue() # Do not process headers.
    endif()

    get_filename_component(SRC_FILE ${SRC_NAME} ABSOLUTE)

    string(REGEX REPLACE "/" "_" SRC_FLAT_NAME ${SRC_NAME})

    set(LLVM_BC_NAME "${SRC_FLAT_NAME}.bc")
    set(LLVM_IR_NAME "${SRC_FLAT_NAME}.ll")
    set(LLVM_AS_NAME "${SRC_FLAT_NAME}.s")

    set(LLVM_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    set(LLVM_BC_FILE "${LLVM_OUT_DIR}/${LLVM_BC_NAME}")
    set(LLVM_IR_FILE "${LLVM_OUT_DIR}/${LLVM_IR_NAME}")
    set(LLVM_AS_FILE "${LLVM_OUT_DIR}/${LLVM_AS_NAME}")

    get_source_file_property(COMPILE_DEFS ${SRC_FILE} COMPILE_DEFINITIONS)
    if(COMPILE_DEFS STREQUAL "NOTFOUND")
      set(COMPILE_DEFS_ARGS "")
    else()
      list(JOIN COMPILE_DEFS " -D" COMPILE_DEFS_ARGS)
      set(COMPILE_DEFS_ARGS "-D${COMPILE_DEFS_ARGS}")
    endif()
    separate_arguments(COMPILE_DEFS_ARGS UNIX_COMMAND ${COMPILE_DEFS_ARGS})

    get_source_file_property(INCLUDE_DIRS ${SRC_FILE} INCLUDE_DIRECTORIES)
    if(INCLUDE_DIRS STREQUAL "NOTFOUND")
      set(INCLUDE_DIRS_ARGS "")
    else()
      list(JOIN INCLUDE_DIRS " -I" INCLUDE_DIRS_ARGS)
      set(INCLUDE_DIRS_ARGS "-I${INCLUDE_DIRS_ARGS}")
    endif()
    separate_arguments(INCLUDE_DIRS_ARGS UNIX_COMMAND ${INCLUDE_DIRS_ARGS})

    get_source_file_property(COMPILE_OPTS_ARGS ${SRC_FILE} COMPILE_OPTIONS)
    if(COMPILE_OPTS_ARGS STREQUAL "NOTFOUND")
      set(COMPILE_OPTS_ARGS "")
    endif()

    # Add options according to what CMake does (default) for add_executable().
    string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE_CHECK)
    if(BUILD_TYPE_CHECK STREQUAL "DEBUG")
      nvs_print("Adding compile options for DEBUG build")
      list(APPEND COMPILE_OPTS_ARGS ${DEBUG_OPTS} -emit-llvm)
    elseif(BUILD_TYPE_CHECK STREQUAL "RELEASE")
      nvs_print("Adding compile options for RELEASE build")
      # For Release builds we also need -ggdb to obtain debug information.
      list(APPEND COMPILE_OPTS_ARGS ${RELEASE_OPTS} -emit-llvm -ggdb)
    endif()

    # This applies user-defined COMPILE_OPTS_ARGS to the source file, which
    # may contain clang's built-in optimizations, e.g. -O2, -O3. Therefore,
    # other user-specified passes will apply after them.
    add_custom_command(
      OUTPUT
        ${LLVM_BC_FILE} ${LLVM_IR_FILE} ${LLVM_AS_FILE}
      DEPENDS
        ${SRC_FILE}
      COMMENT
        "Generating ${LLVM_BC_NAME}, ${LLVM_IR_NAME}, and ${LLVM_AS_NAME}"
      COMMAND
        ${CMAKE_C_COMPILER} ${COMPILE_DEFS_ARGS} ${INCLUDE_DIRS_ARGS} ${COMPILE_OPTS_ARGS} -o ${LLVM_BC_FILE} -c ${SRC_FILE}
      COMMAND
        ${LLVM_TOOLS_BINARY_DIR}/llvm-dis ${LLVM_BC_FILE}
      COMMAND
        ${LLVM_TOOLS_BINARY_DIR}/llc ${LLVM_BC_FILE} --x86-asm-syntax=intel -o ${LLVM_AS_FILE}
      VERBATIM
    )

    add_custom_target(
      ${LLVM_BC_NAME}
      DEPENDS
        ${LLVM_BC_FILE}
    )
  # IR always generates with Bitcode. Don't need this target explicitly unless
  # `make <source>.ll` is desired on the command line.
  #
  # add_custom_target(
  #   ${LLVM_IR_NAME}
  #   DEPENDS
  #     ${LLVM_IR_FILE}
  # )

    get_source_file_property(LLVM_OPT_PASSES ${SRC_FILE} LLVM_OPT_PASSES)
    if(LLVM_OPT_PASSES STREQUAL "NOTFOUND")
      set(LLVM_OPT_PASSES "")
    endif()

    set(PASS_ID "0")
    foreach(LLVM_OPT_PASS ${LLVM_OPT_PASSES})
      math(EXPR PASS_ID "${PASS_ID} + 1")
      set(LLVM_OPT_BC_NAME "${SRC_FLAT_NAME}.opt${PASS_ID}.bc")
      set(LLVM_OPT_IR_NAME "${SRC_FLAT_NAME}.opt${PASS_ID}.ll")
      set(LLVM_OPT_AS_NAME "${SRC_FLAT_NAME}.opt${PASS_ID}.s")
      set(LLVM_OPT_LOG_NAME "${SRC_FLAT_NAME}.opt${PASS_ID}.log")
      set(LLVM_OPT_BC_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_BC_NAME}")
      set(LLVM_OPT_IR_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_IR_NAME}")
      set(LLVM_OPT_AS_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_AS_NAME}")
      set(LLVM_OPT_LOG_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_LOG_NAME}")

      # Convert string to list to remove quotes.
      separate_arguments(PASS_ARGS UNIX_COMMAND ${LLVM_OPT_PASS})
      nvs_print("Will apply LLVM pass '${LLVM_OPT_PASS}': ${LLVM_BC_NAME} -> ${LLVM_OPT_BC_NAME}")

      # Extract dependent pass modules from the arguments.
      set(LLVM_OPT_DEPS "")
      string(REGEX MATCHALL "-load[ \t=]*([^ \t\n\r]*)$?" LLVM_OPT_LOADS ${LLVM_OPT_PASS})
      foreach(LLVM_OPT_LOAD ${LLVM_OPT_LOADS})
        string(REGEX REPLACE "-load[ \t=]*" "" LLVM_OPT_LOAD ${LLVM_OPT_LOAD})
        if(NOT LLVM_OPT_LOAD IN_LIST LLVM_OPT_DEPS)
          list(APPEND LLVM_OPT_DEPS ${LLVM_OPT_LOAD})
        endif()
      endforeach()

      add_custom_command(
        OUTPUT
          ${LLVM_OPT_BC_FILE} ${LLVM_OPT_IR_FILE} ${LLVM_OPT_AS_FILE} ${LLVM_OPT_LOG_FILE}
        DEPENDS
          ${LLVM_BC_FILE} ${LLVM_OPT_DEPS}
        COMMENT
          "Applying LLVM pass '${LLVM_OPT_PASS}': ${LLVM_BC_NAME} -> ${LLVM_OPT_BC_NAME}"
        COMMAND
          ${LLVM_TOOLS_BINARY_DIR}/opt ${PASS_ARGS} ${LLVM_BC_FILE} -stats -o ${LLVM_OPT_BC_FILE} 2> ${LLVM_OPT_LOG_FILE}
        COMMAND
          ${CMAKE_COMMAND} -E echo "       LLVM opt details saved to ${LLVM_OPT_LOG_FILE}"
        COMMAND
          ${LLVM_TOOLS_BINARY_DIR}/llvm-dis ${LLVM_OPT_BC_FILE}
        COMMAND
          ${LLVM_TOOLS_BINARY_DIR}/llc ${LLVM_OPT_BC_FILE} --x86-asm-syntax=intel -o ${LLVM_OPT_AS_FILE}
        VERBATIM
      )

      add_custom_target(
        ${LLVM_OPT_BC_NAME}
        DEPENDS
          ${LLVM_OPT_BC_FILE}
      )

      set(LLVM_BC_NAME "${LLVM_OPT_BC_NAME}")
      set(LLVM_IR_NAME "${LLVM_OPT_IR_NAME}")
      set(LLVM_AS_NAME "${LLVM_OPT_AS_NAME}")
      set(LLVM_BC_FILE "${LLVM_OPT_BC_FILE}")
      set(LLVM_IR_FILE "${LLVM_OPT_IR_FILE}")
      set(LLVM_AS_FILE "${LLVM_OPT_AS_FILE}")
    endforeach()

    list(APPEND LLVM_BC_FILES ${LLVM_BC_FILE})
  endforeach()

# Custom target also works for this function. But CMake does not support custom
# target in some other handy functions, such as target_link_libraries().
#
# add_custom_target(
#   ${EXE_TARGET} ALL
#   DEPENDS
#     ${LLVM_BC_FILES}
#   COMMENT
#     "Generating executable ${EXE_TARGET}"
#   COMMAND
#     ${CMAKE_C_COMPILER} ${LLVM_BC_FILES} -o ${EXE_TARGET}
# )

  set_source_files_properties(
    ${LLVM_BC_FILES}
    PROPERTIES
      EXTERNAL_OBJECT true
      GENERATED true
  )

  add_executable(${EXE_TARGET} ${LLVM_BC_FILES})

  nvs_print("Linker language for executable '${EXE_TARGET}': ${LINKER_LANG}")
  set_target_properties(
    ${EXE_TARGET}
    PROPERTIES
      LINKER_LANGUAGE ${LINKER_LANG}
  )

  add_custom_command(
    TARGET ${EXE_TARGET} POST_BUILD
    BYPRODUCTS ${EXE_TARGET}.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND
      objdump -M intel -S --disassemble ${EXE_TARGET} > ${EXE_TARGET}.s
    VERBATIM
  )
endfunction()
