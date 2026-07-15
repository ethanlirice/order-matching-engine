# M5: L4 market-making strategies, implemented in C++ (PROJECT_SPEC.md
# §4/§8/§11 -- Python's role is evaluation/sweeps/plots via pybind11, not
# strategy authoring). Depends on lob_sim, never the reverse.
add_library(lob_mm STATIC
  market_maker.cpp
)
target_include_directories(lob_mm PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lob_mm PUBLIC lob_sim)
target_link_libraries(lob_mm PRIVATE lob_warnings)
