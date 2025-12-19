#include "test_common.hpp"

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
  dp_init(
      &allocator, buffer.data(),
      BUFFER_SIZE IF_DP_LOG(
          ,
          {.debug = test_debug, .info = test_info, .warning = test_warning, .error = test_error}));

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
