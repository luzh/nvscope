set(SRCS benchmark_time.c)

# Link statically for performance (inline) and initializations before main().
add_library(utils STATIC ${SRCS})

target_compile_options(utils
  PUBLIC -Wall -Wextra -funroll-loops -Wno-pointer-sign -masm=intel
)

target_compile_definitions(utils
  PUBLIC _GNU_SOURCE _FORTIFY_SOURCE=2 $<$<BOOL:${COLORPRINT}>:COLORPRINT>
)
target_include_directories(utils PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
