# One executable per test file; `ctest --preset host -R <name>` runs a single one.
function(scroller_add_test name)
  add_executable(${name} ${ARGN} test_main.cpp)
  target_link_libraries(${name} PRIVATE scroller_core)
  # Test helpers forward format strings that are not literals.
  target_compile_options(${name} PRIVATE -Wno-format-security -Wno-format-nonliteral)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

scroller_add_test(format_test format_test.cpp)
scroller_add_test(viewport_test viewport_test.cpp)
scroller_add_test(present_test present_test.cpp)
scroller_add_test(input_test input_test.cpp)
scroller_add_test(arena_test arena_test.cpp)
