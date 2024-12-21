find_package(benchmark REQUIRED)

add_executable(allocator_benchmark allocator_benchmark.cpp)
target_link_libraries(allocator_benchmark PUBLIC allocator benchmark::benchmark)
