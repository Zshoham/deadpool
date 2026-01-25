include(FetchContent)

FetchContent_Declare(
  googlebench
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.5
  GIT_PROGRESS TRUE
)

FetchContent_Declare(
  mimalloc
  GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
  GIT_TAG v2.1.7
  GIT_PROGRESS TRUE
)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(mimalloc googlebench)

add_executable(allocator_benchmark allocator_benchmark.cpp)
target_link_libraries(allocator_benchmark PRIVATE allocator benchmark::benchmark mimalloc-static)
target_compile_options(allocator_benchmark PRIVATE -O3)
