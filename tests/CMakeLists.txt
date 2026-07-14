add_executable(lob_tests
  scaffold_test.cpp
)

target_link_libraries(lob_tests PRIVATE lob GTest::gtest_main lob_warnings lob_sanitizers)

include(GoogleTest)
gtest_discover_tests(lob_tests)
