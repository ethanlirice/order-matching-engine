add_executable(lob_tests
  order_book_test.cpp
  matching_engine_test.cpp
  determinism_test.cpp
)

target_include_directories(lob_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_tests PRIVATE lob GTest::gtest_main lob_warnings lob_sanitizers)

include(GoogleTest)
gtest_discover_tests(lob_tests)
