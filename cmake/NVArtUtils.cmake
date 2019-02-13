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

function(nvart_print MSGSTR)
  message(STATUS "${Blue}[NVArt-INFO]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvart_debug MSGSTR)
  message(STATUS "${Yellow}[NVArt-Debug]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvart_fatal MSGSTR)
  message(FATAL_ERROR "${BoldRed}[NVArt-Fatal]:${ColorReset} ${MSGSTR}")
endfunction()

function(nvart_set_default_properties)
  nvart_print("Setting default properties for ${ARGV}")
  set_property(
    SOURCE
      ${ARGV}
    PROPERTY INCLUDE_DIRECTORIES
      ${CMAKE_SOURCE_DIR}/include
  )

  set_property(
    SOURCE
      ${ARGV}
    PROPERTY COMPILE_DEFINITIONS
      GNU_SOURCE
      PRINT_COLOR
  )

  set(EXTRA_COMPILE_FLAGS "")
  if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(EXTRA_COMPILE_FLAGS -O3 -DNDEBUG)
  elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(EXTRA_COMPILE_FLAGS -ggdb)
  endif()

  set_property(
    SOURCE
      ${ARGV}
    PROPERTY COMPILE_FLAGS
      -Wall -Wextra -std=gnu99 ${EXTRA_COMPILE_FLAGS} -emit-llvm
  )
endfunction()

function(nvart_add_executable)
  list(LENGTH ARGV ARGS_LEN)
  if(ARGS_LEN LESS "2")
    nvart_fatal("Required arguments: <target> <source1> [source2...]")
  endif()

  list(GET ARGV 0 EXE_TARGET)
  math(EXPR SRCS_LEN "${ARGS_LEN} - 1")
  list(SUBLIST ARGV 1 ${SRCS_LEN} SRC_NAMES)

  nvart_print("Sources for target ${EXE_TARGET}: ${SRC_NAMES}")

  set(LLVM_BC_FILES "")

  foreach(SRC_NAME ${SRC_NAMES})
    get_filename_component(SRC_FILE ${SRC_NAME} ABSOLUTE)
    get_filename_component(SRC_BASE_NAME ${SRC_NAME} NAME_WE)

    set(LLVM_BC_NAME "${SRC_BASE_NAME}.bc")
    set(LLVM_IR_NAME "${SRC_BASE_NAME}.ll")
    set(LLVM_AS_NAME "${SRC_BASE_NAME}.s")

    set(LLVM_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    set(LLVM_BC_FILE "${LLVM_OUT_DIR}/${LLVM_BC_NAME}")
    set(LLVM_IR_FILE "${LLVM_OUT_DIR}/${LLVM_IR_NAME}")
    set(LLVM_AS_FILE "${LLVM_OUT_DIR}/${LLVM_AS_NAME}")

    get_source_file_property(INCLUDE_DIRS ${SRC_FILE} INCLUDE_DIRECTORIES)
    if (INCLUDE_DIRS STREQUAL "NOTFOUND")
      set(INCLUDE_DIRS_ARGS "")
    else()
      list(JOIN INCLUDE_DIRS " -I" INCLUDE_DIRS_ARGS)
      set(INCLUDE_DIRS_ARGS "-I${INCLUDE_DIRS_ARGS}")
    endif()
    separate_arguments(INCLUDE_DIRS_ARGS UNIX_COMMAND ${INCLUDE_DIRS_ARGS})

    get_source_file_property(COMPILE_DEFS ${SRC_FILE} COMPILE_DEFINITIONS)
    if (COMPILE_DEFS STREQUAL "NOTFOUND")
      set(COMPILE_DEFS_ARGS "")
    else()
      list(JOIN COMPILE_DEFS " -D" COMPILE_DEFS_ARGS)
      set(COMPILE_DEFS_ARGS "-D${COMPILE_DEFS_ARGS}")
    endif()
    separate_arguments(COMPILE_DEFS_ARGS UNIX_COMMAND ${COMPILE_DEFS_ARGS})

    get_source_file_property(COMPILE_FLAGS_ARGS ${SRC_FILE} COMPILE_FLAGS)
    if (COMPILE_FLAGS_ARGS STREQUAL "NOTFOUND")
      set(COMPILE_FLAGS_ARGS "")
    endif()

    # This applies user-defined COMPILE_FLAGS_ARGS to the source file, which may
    # contain clang's built-in optimizations, e.g. -O2, -O3. Therefore, other
    # user-specified passes will apply after them.
    add_custom_command(
      OUTPUT
        ${LLVM_BC_FILE} ${LLVM_IR_FILE} ${LLVM_AS_FILE}
      DEPENDS
        ${SRC_FILE}
      COMMENT
        "Generating ${LLVM_BC_NAME}, ${LLVM_IR_NAME}, and ${LLVM_AS_NAME}"
      COMMAND
        ${CMAKE_C_COMPILER} ${INCLUDE_DIRS_ARGS} ${COMPILE_DEFS_ARGS} ${COMPILE_FLAGS_ARGS} -c ${SRC_FILE} -o ${LLVM_BC_FILE}
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
  # add_custom_target(
  #   ${LLVM_IR_NAME}
  #   DEPENDS
  #     ${LLVM_IR_FILE}
  # )

    get_source_file_property(LLVM_PASSES ${SRC_FILE} LLVM_PASSES)
    if (LLVM_PASSES STREQUAL "NOTFOUND")
      set(LLVM_PASSES "")
    endif()

    set(PASS_ID "0")
    foreach(LLVM_PASS ${LLVM_PASSES})
      math(EXPR PASS_ID "${PASS_ID} + 1")
      set(LLVM_OPT_BC_NAME "${SRC_BASE_NAME}.opt${PASS_ID}.bc")
      set(LLVM_OPT_IR_NAME "${SRC_BASE_NAME}.opt${PASS_ID}.ll")
      set(LLVM_OPT_AS_NAME "${SRC_BASE_NAME}.opt${PASS_ID}.s")
      set(LLVM_OPT_BC_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_BC_NAME}")
      set(LLVM_OPT_IR_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_IR_NAME}")
      set(LLVM_OPT_AS_FILE "${LLVM_OUT_DIR}/${LLVM_OPT_AS_NAME}")

      # convert string to list to remove quotes
      separate_arguments(PASS_ARGS UNIX_COMMAND ${LLVM_PASS})
      nvart_print("Will apply LLVM pass '${LLVM_PASS}': ${LLVM_BC_NAME} -> ${LLVM_OPT_BC_NAME}")

      add_custom_command(
        OUTPUT
          ${LLVM_OPT_BC_FILE} ${LLVM_OPT_IR_FILE} ${LLVM_OPT_AS_FILE}
        DEPENDS
          ${LLVM_BC_FILE}
        COMMENT
          "Applying LLVM pass '${LLVM_PASS}': ${LLVM_BC_NAME} -> ${LLVM_OPT_BC_NAME}"
        COMMAND
          ${LLVM_TOOLS_BINARY_DIR}/opt ${PASS_ARGS} ${LLVM_BC_FILE} -o ${LLVM_OPT_BC_FILE}
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

  add_custom_target(
    ${EXE_TARGET} ALL
    DEPENDS
      ${LLVM_BC_FILES}
    COMMENT
      "Generating executable ${EXE_TARGET}"
    COMMAND
      ${CMAKE_C_COMPILER} ${LLVM_BC_FILES} -o ${EXE_TARGET}
  )
endfunction()
