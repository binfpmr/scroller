# Host build: platform-independent code plus unit tests, using the normal hosted toolchain.

add_compile_options(-Wall -Wextra -Werror)
add_library(scroller_core STATIC ${SCROLLER_CORE_SOURCES})
target_include_directories(scroller_core PUBLIC src)

# Level analysis tool (see tools/levelcheck/reachability.h).
add_library(levelcheck_lib STATIC tools/levelcheck/reachability.cpp)
target_link_libraries(levelcheck_lib PUBLIC scroller_core)
target_include_directories(levelcheck_lib PUBLIC tools/levelcheck)
add_executable(levelcheck tools/levelcheck/levelcheck.cpp)
target_link_libraries(levelcheck PRIVATE levelcheck_lib)

# Renders a song through the audio engine into a WAV file.
add_executable(render_audio tools/render_audio/render_audio.cpp)
target_link_libraries(render_audio PRIVATE scroller_core)

enable_testing()
add_subdirectory(tests)
