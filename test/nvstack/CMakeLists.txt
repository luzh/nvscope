set(PROG nvstack)
set(SRCS nvstack.c)

nvs_set_sources_properties(C_Default ${SRCS})

set_property(
  SOURCE
    ${SRCS}
  PROPERTY LLVM_OPT_PASSES
    "-load ${NVS_PASSES_DIR}/NVScope/NVScope.so -probes"
)

nvs_add_executable(${PROG} ${SRCS})

add_dependencies(${PROG} NVScope)

target_link_libraries(${PROG} nvsrt)

set(STACKFILE "./stackfile")

add_custom_target(
  push-${PROG} DEPENDS ${PROG} COMMENT "Running ${PROG} (push) ..."
  COMMAND
    test -f ${STACKFILE} || dd if=/dev/zero of=${STACKFILE} bs=4k count=1
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} push
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} show
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} check
)

add_custom_target(
  pop-${PROG} DEPENDS ${PROG} COMMENT "Running ${PROG} (pop) ..."
  COMMAND
    test -f ${STACKFILE} || dd if=/dev/zero of=${STACKFILE} bs=4k count=1
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} pop
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} show
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} check
)

add_custom_target(
  show-${PROG} DEPENDS ${PROG} COMMENT "Running ${PROG} (show) ..."
  COMMAND
    test -f ${STACKFILE} || dd if=/dev/zero of=${STACKFILE} bs=4k count=1
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} show
)

add_custom_target(
  check-${PROG} DEPENDS ${PROG} COMMENT "Running ${PROG} (check) ..."
  COMMAND
    test -f ${STACKFILE} || dd if=/dev/zero of=${STACKFILE} bs=4k count=1
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} check
)

add_custom_target(
  fuzz-push-${PROG} DEPENDS ${PROG} COMMENT "Testing ${PROG} (push) ..."
  DEPENDS nvfuzz
  COMMAND
    test -f ${STACKFILE} || dd if=/dev/zero of=${STACKFILE} bs=4k count=1
  COMMAND
    ${NVS_TOOLS_DIR}/nvfuzz ${CMAKE_CURRENT_BINARY_DIR}/${PROG} ${STACKFILE} push
)
