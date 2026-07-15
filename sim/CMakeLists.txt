# M4: L3 simulator layer. Depends on lob (L1), never the reverse -- keeps
# the layer boundary (PROJECT_SPEC.md §4) enforced structurally, not just
# by convention. Header-only for M4 Step 0; gains sources in later steps
# (synthetic_generator.cpp, simulator.cpp).
add_library(lob_sim INTERFACE)
target_include_directories(lob_sim INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lob_sim INTERFACE lob)
