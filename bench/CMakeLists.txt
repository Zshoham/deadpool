find_package(benchmark REQUIRED)

add_executable(allocator_benchmark allocator_benchmark.cpp)
target_link_libraries(allocator_benchmark PUBLIC allocator benchmark::benchmark)
# target_compile_options(allocator_benchmark PRIVATE -O3)

target_compile_options(allocator_benchmark PRIVATE
    -g3 -O0 -ggdb
)
# target_link_options(allocator_benchmark PRIVATE
#     -g -O1 -fsanitize=address,undefined
# )


