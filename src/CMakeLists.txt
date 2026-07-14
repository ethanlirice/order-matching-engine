add_library(lob STATIC
  level.cpp
  order_book.cpp
  matching_engine.cpp
)

target_include_directories(lob PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lob PUBLIC lob_sanitizers)
target_link_libraries(lob PRIVATE lob_warnings)
