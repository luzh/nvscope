set(SRCS condstore.c)

add_library(nvart SHARED ${SRCS})

target_compile_features(nvart PUBLIC c_std_99)
target_compile_options(nvart PUBLIC -Wall -Wextra -masm=intel)
target_compile_definitions(nvart PUBLIC GNU_SOURCE PRINT_COLOR)
target_include_directories(nvart PUBLIC ${CMAKE_HOME_DIRECTORY}/include)
