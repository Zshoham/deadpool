cmake_minimum_required(VERSION 4.0)
project(memory_allocator C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_COVERAGE "Enable test coverage generation" OFF)

include(cmake/generate_config.cmake)
generate_config_helpers(
  CONFIG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/config.h"
)

add_library(allocator src/allocator.c)
add_dependencies(allocator gen_config_headers)
target_include_directories(allocator PUBLIC ${GENERATED_HEADER_DIR})

if(ENABLE_COVERAGE)
  target_compile_options(allocator PRIVATE -coverage)
  target_link_options(allocator PRIVATE -coverage)
  target_link_libraries(allocator PUBLIC gcov)
endif()

# Optionally, if you have a header file:
target_include_directories(allocator PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

install(TARGETS allocator DESTINATION lib)
install(FILES allocator.h DESTINATION include)

if(ENABLE_TESTS)
  target_compile_definitions(allocator PUBLIC
    -DDP_LOG=1 -DDP_STATS=1 -DDP_FREE_VALIDATION=1
  )
  add_subdirectory(test)
endif()

if(ENABLE_BENCHMARKS)
  add_subdirectory(bench)
endif()
