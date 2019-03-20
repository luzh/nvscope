set(SRCS nvsrt.c)

# Link statically for performance (inline) and initializations before main().
add_library(nvsrt STATIC ${SRCS})

target_compile_features(nvsrt PUBLIC c_std_99)
target_compile_options(
  nvsrt PUBLIC -Wall -Wextra -funroll-loops -Wno-pointer-sign -masm=intel
)

# Todo: Define NVS_DEBUG according to build types
target_compile_definitions(nvsrt
  PUBLIC COLORFUL _GNU_SOURCE _FORTIFY_SOURCE=2 $<$<BOOL:${NVS_DEBUG}>:NVS_DEBUG>
)
target_include_directories(nvsrt PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
