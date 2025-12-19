include(CTest)

# Find all test files and create executables
file(GLOB TEST_SOURCES "*test.cpp")
set(TEST_FLAGS -g -O0 -fsanitize=address,undefined)
add_custom_target(tests ALL)

foreach(TEST_SOURCE ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
  set(EXECUTABLE_NAME "allocator_${TEST_NAME}")

  add_executable(${EXECUTABLE_NAME} ${TEST_SOURCE})
  target_link_libraries(
      ${EXECUTABLE_NAME} PRIVATE allocator GTest::gtest_main
  )
  target_compile_options(${EXECUTABLE_NAME} PRIVATE ${TEST_FLAGS})
  target_link_options(${EXECUTABLE_NAME} PRIVATE ${TEST_FLAGS})

  add_dependencies(tests ${EXECUTABLE_NAME})
  add_test(NAME ${EXECUTABLE_NAME} COMMAND ${EXECUTABLE_NAME})
endforeach()

# Fuzzer test (separate configuration)
add_executable(allocator_fuzz allocator_fuzz.cpp)
target_link_libraries(allocator_fuzz PRIVATE allocator)

target_compile_options(allocator_fuzz PRIVATE
    -g -O0 -fsanitize=fuzzer,address,undefined
)
target_link_options(allocator_fuzz PRIVATE
    -g -O0 -fsanitize=fuzzer,address,undefined
)
