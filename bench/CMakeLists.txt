add_executable(internal_benchmark internal_benchmark.cpp)
target_link_libraries(internal_benchmark PUBLIC benchmark::benchmark)
target_link_options(internal_benchmark PRIVATE -g)

add_executable(allocator_benchmark allocator_benchmark.cpp)
target_link_libraries(allocator_benchmark PUBLIC allocator benchmark::benchmark)

