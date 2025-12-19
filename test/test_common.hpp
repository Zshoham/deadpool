#pragma once

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <vector>

#include "allocator.h"
#include "config_macros.h"

static constexpr size_t DEFAULT_ALIGN = alignof(max_align_t);

static inline size_t align_up(size_t value, size_t alignment) {
  return (value + (alignment - 1)) & ~(alignment - 1);
}

inline void test_debug(const char *fmt, ...) {
  printf("DEBUG: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

inline void test_info(const char *fmt, ...) {
  printf("INFO: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

inline void test_warning(const char *fmt, ...) {
  printf("WARNING: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

inline void test_error(const char *fmt, ...) {
  printf("ERROR: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

struct AllocationMetadata {
  void *ptr;
  size_t size;

  bool operator<(const AllocationMetadata &other) const { return ptr < other.ptr; }

  bool operator==(const AllocationMetadata &other) const { return ptr == other.ptr; }
};

class DPAllocatorTest : public ::testing::Test {
protected:
  static constexpr size_t BUFFER_SIZE = 1024;
  std::array<uint8_t, BUFFER_SIZE> buffer;
  dp_alloc allocator;
  std::vector<AllocationMetadata> allocated;
  size_t total_allocated;

  void SetUp() override {
    buffer.fill(0);
    total_allocated = 0;
    dp_init(&allocator, buffer.data(),
            BUFFER_SIZE IF_DP_LOG(, {.debug = test_debug,
                                     .info = test_info,
                                     .warning = test_warning,
                                     .error = test_error}));
  }

  size_t available() {
    size_t total_used = 0;
    for (auto allocation : allocated) {
      size_t user_alloc = DEFAULT_ALIGN + allocation.size;
      size_t block_size = align_up(sizeof(block_header) + user_alloc, DEFAULT_ALIGN);
      total_used += block_size;
    }
    return BUFFER_SIZE - (total_used + sizeof(block_header));
  }

  void checked_alloc(size_t alloc_size, void **result = NULL) {
    void *allocated_ptr;
    allocated_ptr = dp_malloc(&allocator, alloc_size);
    allocated.push_back({allocated_ptr, alloc_size});
    ASSERT_NE(nullptr, allocated_ptr);
    if (allocator.free_list_head != nullptr) {
      ASSERT_TRUE(allocator.free_list_head->is_free);
    }

    if (result != NULL) {
      *result = allocated_ptr;
    }
  }

  void checked_free(void *ptr) {
    auto erased = std::erase_if(allocated, [&](auto allocation) { return allocation.ptr == ptr; });
    ASSERT_GT(erased, 0);
    ASSERT_EQ(dp_free(&allocator, ptr), 0);
  }

  void TearDown() override {
    if (::testing::Test::HasFailure()) {
      test_warning("Not running teardown checks because the test already "
                   "failed elsewhere.");
      return;
    }
    for (auto allocation : allocated) {
      ASSERT_NE(allocation.ptr, nullptr);
      ASSERT_EQ(dp_free(&allocator, allocation.ptr), 0);
    }
    allocated.clear();
  }
};
