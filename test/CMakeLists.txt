include(CTest)

add_executable(allocator_test allocator_test.cpp)
target_link_libraries(allocator_test PRIVATE allocator GTest::gtest_main)

target_compile_options(allocator_test PRIVATE
    -g -O1 -fsanitize=address,undefined
)
target_link_options(allocator_test PRIVATE
    -g -O0 -fsanitize=address,undefined
)

add_test(NAME dyn_alloc_test COMMAND allocator_test)

add_executable(allocator_fuzz allocator_fuzz.cpp)
target_link_libraries(allocator_fuzz PRIVATE allocator)

target_compile_options(allocator_fuzz PRIVATE
    -g -O1 -fsanitize=fuzzer,address,undefined
)
target_link_options(allocator_fuzz PRIVATE
    -g -O1 -fsanitize=fuzzer,address,undefined
)
