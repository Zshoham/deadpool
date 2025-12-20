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
  test_info("Complexity check: N=%d, iterations=%zu\n", N / 2, allocator.num_iterations);
  ASSERT_GE(allocator.num_iterations, N / 2);
#endif
}
