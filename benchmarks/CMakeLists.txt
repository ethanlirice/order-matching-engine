# `lob` links lob_sanitizers PUBLICLY (src/CMakeLists.txt), so its flags
# still reach this target transitively whenever the whole project is
# configured with -DLOB_ENABLE_SANITIZERS=ON. ASan/UBSan instrumentation
# overhead invalidates latency measurements, so: always configure with
# LOB_ENABLE_SANITIZERS=OFF (the default) when collecting real benchmark
# numbers -- use a separate build directory from the sanitized test build
# if you need both at once. See README's Benchmark section.
add_executable(lob_bench lob_bench.cpp spsc_ring_buffer_bench.cpp)
target_include_directories(lob_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_bench PRIVATE lob benchmark::benchmark lob_warnings Threads::Threads)
