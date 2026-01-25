include(CTest)
include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_PROGRESS TRUE
)
FetchContent_Declare(
  googlefuzz
  GIT_REPOSITORY https://github.com/google/fuzztest.git
  GIT_TAG 8359318f6b5bde6436d58eec9ee1f9e74be553d5
  GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(googletest googlefuzz)

# Find all test files and create executables (excluding fuzztest files)
file(GLOB TEST_SOURCES "*test.cpp")

set(TEST_FLAGS -fsanitize=address,undefined)
add_custom_target(tests)

foreach(test_source ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${test_source} NAME_WE)
  set(EXECUTABLE_NAME "allocator_${TEST_NAME}")

  add_executable(${EXECUTABLE_NAME} ${test_source})
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

