#include <algorithm>
#include <ranges>
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

void test_debug(const char *fmt, ...) {
  printf("DEBUG: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void test_info(const char *fmt, ...) {
  printf("INFO: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void test_warning(const char *fmt, ...) {
  printf("WARNING: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void test_error(const char *fmt, ...) {
  printf("ERROR: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

class DPAllocatorTest : public ::testing::Test {
protected:
  struct AllocationMetadata {
    void *ptr;
    size_t size;

    bool operator<(const AllocationMetadata &other) const {
      return ptr < other.ptr;
    }

    bool operator==(const AllocationMetadata &other) const {
      return ptr == other.ptr;
    }
  };

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
    auto erased =
        std::erase_if(allocated,
                     [&](auto allocation) { return allocation.ptr == ptr; });
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

// Basic Allocation Tests
TEST_F(DPAllocatorTest, SingleAllocation) {
  void *result;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &result));
}

TEST_F(DPAllocatorTest, MultipleAllocations) {
  size_t allocated_size = 0;
  void *ptr;
  for (int i = 0; i < 5; i++) {
    ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &ptr));
  }

  // Verify all pointers are different
  std::sort(allocated.begin(), allocated.end());
  ASSERT_EQ(allocated.end(),
            std::adjacent_find(allocated.begin(), allocated.end()));
}

// Fragmentation Tests
TEST_F(DPAllocatorTest, FragmentationAndCoalescing) {
  void *ptr1, *ptr2, *ptr3, *ptr4;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &ptr1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &ptr2));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &ptr3));

  // Create fragmentation by freeing middle block
  ASSERT_NO_FATAL_FAILURE(checked_free(ptr2));

  // Allocate slightly smaller block - should fit in the gap
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &ptr4));
  block_header *p4_block =
      (block_header *)((uint8_t *)ptr4 - sizeof(block_header));

  // Free all blocks
  ASSERT_NO_FATAL_FAILURE(checked_free(ptr1));
  ASSERT_NO_FATAL_FAILURE(checked_free(ptr3));
  ASSERT_NO_FATAL_FAILURE(checked_free(ptr4));

  // Should be able to allocate a large block now
  void *large_ptr;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(900, &large_ptr));
}

TEST_F(DPAllocatorTest, FragmentedTooLargeAllocationFailure) {
  std::vector<void *> ptrs;
  while (true) {
    void *p = dp_malloc(&allocator, 64);
    if (p == nullptr)
      break;
    ptrs.push_back(p);
    allocated.push_back({p, 64});
  }
  ASSERT_GT(ptrs.size(), 4);

  for (size_t i = 1; i < ptrs.size() - 1; i += 2) {
    ASSERT_NO_FATAL_FAILURE(checked_free(ptrs[i]));
  }

  ASSERT_EQ(dp_malloc(&allocator, 200), (void *)NULL);
}


// Edge Cases
TEST_F(DPAllocatorTest, ZeroSizeAllocation) {
  void *ptr = dp_malloc(&allocator, 0);
  ASSERT_EQ(nullptr, ptr);
}

TEST_F(DPAllocatorTest, TooLargeAllocation) {
  void *ptr = dp_malloc(&allocator, BUFFER_SIZE + 1);
  ASSERT_EQ(nullptr, ptr);
}

TEST_F(DPAllocatorTest, ExactSizeAllocation) {
  void *ptr = dp_malloc(&allocator, BUFFER_SIZE - 2 * sizeof(block_header));
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(nullptr, dp_malloc(&allocator, 1)); // Should be full
}

// Alignment Tests
TEST_F(DPAllocatorTest, PointerAlignment) {
  void *ptr = dp_malloc(&allocator, 1);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0, reinterpret_cast<uintptr_t>(ptr) % sizeof(void *));
}

// Stress Tests
TEST_F(DPAllocatorTest, AlternatingAllocationFreeing) {
  const int NUM_ITERATIONS = 100;
  const size_t ALLOC_SIZE = 16;
  void *ptr;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    ASSERT_NO_FATAL_FAILURE(checked_alloc(ALLOC_SIZE, &ptr));
    ASSERT_NO_FATAL_FAILURE(checked_free(ptr));
  }
}

TEST_F(DPAllocatorTest, RandomizedAllocationsAndFrees) {
  const int NUM_ALLOCS = 10;
  void *ptr;

  // Random allocations
  for (int i = 0; i < NUM_ALLOCS; i++) {
    size_t size = rand() % 64 + 1; // Random size between 1 and 64
    ASSERT_NO_FATAL_FAILURE(checked_alloc(size, &ptr));
  }

  // Random frees
  while (!allocated.empty()) {
    size_t index = rand() % allocated.size();
    ASSERT_NO_FATAL_FAILURE(checked_free(allocated[index].ptr));
  }
}

// Invalid Usage Tests
TEST_F(DPAllocatorTest, DoubleFree) {
  void *ptr = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr);

  dp_free(&allocator, ptr);
  dp_free(&allocator, ptr); // Should handle this gracefully

  // Should still be able to allocate
  void *new_ptr = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, new_ptr);
}

TEST_F(DPAllocatorTest, FreeNullPtr) {
  dp_free(&allocator, nullptr); // Should handle this gracefully
}

TEST_F(DPAllocatorTest, BestFitNotHead) {
  void *p1, *barrier, *p2, *p3;
  // Alloc blocks with barrier to prevent coalescing
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(10, &barrier)); // Barrier
  ASSERT_NO_FATAL_FAILURE(checked_alloc(200, &p2));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p3));

  // Free p1 (100) -> Head
  ASSERT_NO_FATAL_FAILURE(checked_free(p1));
  // Free p2 (200) -> Head -> p1 (barrier prevents merge)
  ASSERT_NO_FATAL_FAILURE(checked_free(p2));

  // Current free list: p2 (200) -> p1 (100)
  // We have fragmentation (2 free blocks), so checked_alloc's available() check
  // will fail.

  // 1. Perfect fit for p1 (100)
  // p2 (200) fit = 100
  // p1 (100) fit = 0 -> Best fit
  // Should take p1, which is NOT head.
  void *p4 = dp_malloc(&allocator, 100);
  ASSERT_NE(p4, nullptr);
  allocated.push_back({p4, 100}); // Track it for TearDown
  ASSERT_EQ(p4, p1);              // Should reuse p1

  // Free p4 (p1) again.
  ASSERT_NO_FATAL_FAILURE(checked_free(p4));

  // List: p1 (100) -> p2 (200).

  // We want p2 to be head again to test the splitting case.
  // Resetting by clearing everything is easiest.
  allocated.clear();
  // Re-init allocator
  buffer.fill(0);
  dp_init(&allocator, buffer.data(),
          BUFFER_SIZE IF_DP_LOG(, {.debug = test_debug,
                                   .info = test_info,
                                   .warning = test_warning,
                                   .error = test_error}));

  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(10, &barrier));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(200, &p2));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p3));

  ASSERT_NO_FATAL_FAILURE(checked_free(p1));
  ASSERT_NO_FATAL_FAILURE(checked_free(p2));
  // List: p2 (200) -> p1 (100)

  // 2. Split fit for p1
  // Alloc 50.
  // p2 (200) fit = 150
  // p1 (100) fit = 50 -> Best fit
  // Should take p1, split it.
  void *p5 = dp_malloc(&allocator, 50);
  ASSERT_NE(p5, nullptr);
  allocated.push_back({p5, 50});

  // Check that p2 is still free and head (size includes alignment padding)
  ASSERT_GE(allocator.free_list_head->size, 200);
}

TEST_F(DPAllocatorTest, Complexity) {
  // Create a fragmented list with N blocks
  const int N = 20;
  std::vector<void *> ptrs;
  for (int i = 0; i < N; ++i) {
    void *p;
    checked_alloc(10, &p); // Small allocs
    ptrs.push_back(p);
  }

  // Free every other block to create holes
  for (int i = 0; i < N; i += 2) {
    checked_free(ptrs[i]);
  }

  // Now we have N/2 free blocks of size 10.
  // Check list length
  size_t list_len = 0;
  block_header *curr = allocator.free_list_head;
  while (curr) {
    list_len++;
    curr = curr->next;
  }
  test_info("List length: %zu\n", list_len);

  // Allocating 9. Fit = 1.
  // It should traverse all blocks to see if there is a better fit (e.g. size
  // 9). Since all are 10, it will traverse all.

  void *p;
  p = dp_malloc(&allocator, 9);
  ASSERT_NE(p, nullptr);
  allocated.push_back({p, 9});

// Check num_iterations
// It should be at least N/2 (number of free blocks).
#if DP_STATS
  test_info("Complexity check: N=%d, iterations=%zu\n", N / 2,
            allocator.num_iterations);
  ASSERT_GE(allocator.num_iterations, N / 2);
#endif
}

TEST_F(DPAllocatorTest, FragmentationMetric) {
// Calculate fragmentation metric: 1 - (largest_free / total_free)

// Initial state: 1 large block. Fragmentation should be 0.
#if DP_STATS
  ASSERT_FLOAT_EQ(dp_get_fragmentation(&allocator), 0.0f);
#endif

  // Fragment it
  // We need to fill the buffer first to avoid tail merging effects.
  allocated.clear();
  buffer.fill(0);
  dp_init(&allocator, buffer.data(),
          BUFFER_SIZE IF_DP_LOG(, {.debug = test_debug,
                                   .info = test_info,
                                   .warning = test_warning,
                                   .error = test_error}));

  void *p1, *p2, *p3, *tail;
  checked_alloc(100, &p1);
  checked_alloc(100, &p2);
  checked_alloc(100, &p3);

  // Alloc rest
  // We want to fill the remaining space.
  // allocator.available is the size of the free block payload.
  // We need to account for the header of the new block.
  // So we can alloc at most available - sizeof(block_header).

  if (allocator.available > sizeof(block_header)) {
    checked_alloc(allocator.available - sizeof(block_header), &tail);
  }

  // Now buffer is full (or close to).
  // Free p1 and p3.
  checked_free(p1);
  checked_free(p3);

  // Free list: p3 (100) -> p1 (100).
  // Total free: 200. Largest: 100.
  // Frag = 1 - 100/200 = 0.5.

#if DP_STATS
  float fragmentation = dp_get_fragmentation(&allocator);
  ASSERT_NEAR(fragmentation, 0.5f, 0.01f);
  test_info("Fragmentation check: %f\n", fragmentation);
#endif
}
