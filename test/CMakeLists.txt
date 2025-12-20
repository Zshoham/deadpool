include(CTest)

# Find all test files and create executables (excluding fuzztest files)
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
  gtest_discover_tests(${EXECUTABLE_NAME})
endforeach()

# FuzzTest-based fuzz tests (separate target for fuzzing mode)
if(FUZZTEST_FUZZING_MODE)
  fuzztest_setup_fuzzing_flags()
endif()

add_executable(allocator_fuzz allocator_fuzz.cpp)
target_link_libraries(allocator_fuzz PRIVATE allocator)
link_fuzztest(allocator_fuzz)
gtest_discover_tests(allocator_fuzz)
add_dependencies(tests allocator_fuzz)

