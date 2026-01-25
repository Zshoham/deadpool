include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_PROGRESS TRUE
)
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

FetchContent_Declare(
  o1heap
  GIT_REPOSITORY https://github.com/pavel-kirienko/o1heap.git
  GIT_TAG master
  GIT_PROGRESS TRUE
  SOURCE_SUBDIR o1heap
)

FetchContent_MakeAvailable(googletest googlebench mimalloc o1heap)

add_library(o1heap_lib STATIC ${o1heap_SOURCE_DIR}/o1heap/o1heap.c)
target_include_directories(o1heap_lib PUBLIC ${o1heap_SOURCE_DIR}/o1heap)

add_executable(allocator_benchmark allocator_benchmark.cpp)
target_link_libraries(allocator_benchmark PRIVATE allocator benchmark::benchmark mimalloc-static o1heap_lib)
target_compile_options(allocator_benchmark PRIVATE -O3)
