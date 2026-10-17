# Host build: platform-independent code plus unit tests, using the normal hosted toolchain.

add_compile_options(-Wall -Wextra -Werror)
add_library(scroller_core STATIC ${SCROLLER_CORE_SOURCES})
target_include_directories(scroller_core PUBLIC src)

enable_testing()
add_subdirectory(tests)
