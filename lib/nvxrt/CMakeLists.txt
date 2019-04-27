set(SRCS nvx_runtime.cc nvx_interface.cc)

# Link statically for performance (inline) and initializations before main().
add_library(clang_rt.nvx STATIC ${SRCS})

# may use -stdlib=libstdc++
target_compile_options(clang_rt.nvx
  PUBLIC -Wall -Wextra -Ofast -funroll-loops -Wno-pointer-sign -masm=intel
)

target_compile_definitions(clang_rt.nvx
  PUBLIC
    _GNU_SOURCE _FORTIFY_SOURCE=2
    $<$<BOOL:${NVX_DEBUG}>:NVX_DEBUG>
    $<$<BOOL:${COLORPRINT}>:COLORPRINT>
)
target_include_directories(clang_rt.nvx PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
target_link_options(clang_rt.nvx PUBLIC -pthread)
