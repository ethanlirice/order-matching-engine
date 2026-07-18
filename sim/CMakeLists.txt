# M4: L3 simulator layer. Depends on lob (L1), never the reverse -- keeps
# the layer boundary (PROJECT_SPEC.md §4) enforced structurally, not just
# by convention.
add_library(lob_sim STATIC
  synthetic_generator.cpp
  simulator.cpp
  market_data_log.cpp
  lobster_replay.cpp
)
target_include_directories(lob_sim PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lob_sim PUBLIC lob)
target_link_libraries(lob_sim PRIVATE lob_warnings)
