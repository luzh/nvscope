set(SRCS nvsrt.cpp probes.cpp)

# Link statically for performance (inline) and initializations before main().
add_library(nvsrt STATIC ${SRCS})

# may use -stdlib=libstdc++
target_compile_options(nvsrt
  PUBLIC -Wall -Wextra -Ofast -funroll-loops -Wno-pointer-sign -masm=intel
)

target_compile_definitions(nvsrt
  PUBLIC
    _GNU_SOURCE _FORTIFY_SOURCE=2
    $<$<BOOL:${NVS_DEBUG}>:NVS_DEBUG>
    $<$<BOOL:${COLORPRINT}>:COLORPRINT>
)
target_include_directories(nvsrt PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
