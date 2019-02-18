set(SRCS nvart.c)

# Link statically for performance (inline) and initializations before main().
add_library(nvart STATIC ${SRCS})

target_compile_features(nvart PUBLIC c_std_99)
target_compile_options(nvart PUBLIC -Wall -Wextra -masm=intel)
target_compile_definitions(nvart PUBLIC _GNU_SOURCE PRINT_COLOR _FORTIFY_SOURCE=2)
target_include_directories(nvart PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
