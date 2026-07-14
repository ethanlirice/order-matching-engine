add_executable(lob_tests
  order_book_test.cpp
  matching_engine_test.cpp
  determinism_test.cpp
  order_pool_test.cpp
)

target_include_directories(lob_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_tests PRIVATE lob GTest::gtest_main lob_warnings lob_sanitizers)

include(GoogleTest)
gtest_discover_tests(lob_tests)

if(LOB_BUILD_FUZZERS)
  add_executable(lob_fuzz_matching fuzz/fuzz_matching.cpp)
  target_include_directories(lob_fuzz_matching PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lob_fuzz_matching PRIVATE lob)
  target_compile_options(lob_fuzz_matching PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(lob_fuzz_matching PRIVATE -fsanitize=fuzzer,address,undefined)

  # Short smoke run on every CI build (mirrors the spec's benchmark-smoke-
  # check philosophy) -- catches a build/regression immediately without
  # requiring a full local fuzzing campaign, which stays a manual step:
  #   ./build/tests/lob_fuzz_matching -max_total_time=60
  add_test(
    NAME fuzz_matching_smoke
    COMMAND lob_fuzz_matching -max_total_time=10 -seed=1
  )
endif()
