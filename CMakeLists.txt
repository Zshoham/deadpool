cmake_minimum_required(VERSION 3.10)
project(memory_allocator C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_COVERAGE "Enable test coverage generation" OFF)

add_library(allocator src/allocator.c)

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

# Add subdirectories for source, tests, and benchmarks
add_subdirectory(test)
add_subdirectory(bench)
