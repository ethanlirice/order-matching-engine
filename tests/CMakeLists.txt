add_executable(lob_tests
  order_book_test.cpp
  matching_engine_test.cpp
  determinism_test.cpp
  order_pool_test.cpp
  spsc_ring_buffer_test.cpp
  sim/event_ordering_test.cpp
  sim/synthetic_generator_test.cpp
  sim/golden_replay_test.cpp
  sim/queue_position_test.cpp
  sim/simulator_determinism_test.cpp
  sim/order_ack_test.cpp
  sim/market_data_log_test.cpp
  sim/time_utils_test.cpp
)

target_include_directories(lob_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_tests PRIVATE lob_sim GTest::gtest_main lob_warnings lob_sanitizers
                       Threads::Threads)

include(GoogleTest)
gtest_discover_tests(lob_tests)

if(LOB_BUILD_TSAN_TESTS)
  # Separate binary, deliberately NOT linked against lob: TSan is
  # incompatible with the ASan/UBSan flags lob links in PUBLIC whenever
  # LOB_ENABLE_SANITIZERS=ON, and SpscRingBuffer is header-only, so this
  # test needs nothing from the library anyway.
  add_executable(lob_tsan_tests spsc_ring_buffer_test.cpp)
  target_include_directories(lob_tsan_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                     ${CMAKE_SOURCE_DIR}/include)
  target_link_libraries(lob_tsan_tests PRIVATE GTest::gtest_main lob_warnings Threads::Threads)
  target_compile_options(lob_tsan_tests PRIVATE -fsanitize=thread)
  target_link_options(lob_tsan_tests PRIVATE -fsanitize=thread)
  gtest_discover_tests(lob_tsan_tests)
endif()

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
