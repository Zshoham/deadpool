#include <cstring>

#include "test_common.hpp"

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
  block_header *p4_block = (block_header *)((uint8_t *)ptr4 - sizeof(block_header));

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
  dp_init(
      &allocator, buffer.data(),
      BUFFER_SIZE, IF_DP_LOG(
          {.debug = test_debug, .info = test_info, .warning = test_warning, .error = test_error}));

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
  test_info("Complexity check: N=%d, iterations=%zu\n", N / 2, allocator.num_iterations);
  ASSERT_GE(allocator.num_iterations, N / 2);
#endif
}

// Non-aligned size tests
TEST_F(DPAllocatorTest, NonAlignedSizeAllocations) {
  void *ptr1, *ptr2, *ptr3;

  // Allocate sizes that are not multiples of DEFAULT_ALIGN
  ASSERT_NO_FATAL_FAILURE(checked_alloc(1, &ptr1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(7, &ptr2));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(13, &ptr3));

  // Verify all pointers are properly aligned
  ASSERT_EQ(0, reinterpret_cast<uintptr_t>(ptr1) % DEFAULT_ALIGN);
  ASSERT_EQ(0, reinterpret_cast<uintptr_t>(ptr2) % DEFAULT_ALIGN);
  ASSERT_EQ(0, reinterpret_cast<uintptr_t>(ptr3) % DEFAULT_ALIGN);

  // Verify we can write to and read from the allocated memory
  memset(ptr1, 0xAA, 1);
  memset(ptr2, 0xBB, 7);
  memset(ptr3, 0xCC, 13);

  ASSERT_EQ(0xAA, *static_cast<uint8_t *>(ptr1));
  ASSERT_EQ(0xBB, *static_cast<uint8_t *>(ptr2));
  ASSERT_EQ(0xCC, *static_cast<uint8_t *>(ptr3));
}

TEST_F(DPAllocatorTest, OddSizeAllocationsSequence) {
  std::vector<void *> ptrs;

  // Allocate a variety of odd sizes
  size_t odd_sizes[] = {3, 5, 11, 17, 23, 31, 37, 41};
  for (size_t sz : odd_sizes) {
    void *p;
    ASSERT_NO_FATAL_FAILURE(checked_alloc(sz, &p));
    ptrs.push_back(p);
    ASSERT_EQ(0, reinterpret_cast<uintptr_t>(p) % DEFAULT_ALIGN);
  }

  // Free every other and reallocate
  for (size_t i = 0; i < ptrs.size(); i += 2) {
    ASSERT_NO_FATAL_FAILURE(checked_free(ptrs[i]));
  }

  // Allocate different odd sizes into the freed slots
  for (size_t i = 0; i < 4; i++) {
    void *p;
    ASSERT_NO_FATAL_FAILURE(checked_alloc(9 + i * 2, &p));
    ptrs.push_back(p);
    ASSERT_EQ(0, reinterpret_cast<uintptr_t>(p) % DEFAULT_ALIGN);
  }
}

// Perfect fit tests - ensure no overflow when block size exactly matches
TEST_F(DPAllocatorTest, PerfectFitNoOverflow) {
  // Create a known-size free block by allocating and freeing
  void *p1, *barrier, *p2;

  ASSERT_NO_FATAL_FAILURE(checked_alloc(64, &p1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(16, &barrier)); // Prevents coalescing
  ASSERT_NO_FATAL_FAILURE(checked_alloc(64, &p2));

  // Get the actual block size that was allocated for p1
  block_header *p1_header = (block_header *)((uint8_t *)p1 - sizeof(block_header) -
                                             (*((uint8_t *)p1 - 1) - sizeof(block_header)));

  // Free p1 to create a free block
  ASSERT_NO_FATAL_FAILURE(checked_free(p1));

  // Now allocate exactly the same size - should get perfect fit
  void *p3;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(64, &p3));

  // Should reuse the same slot
  ASSERT_EQ(p3, p1);
}

TEST_F(DPAllocatorTest, PerfectFitMultipleSizes) {
  // Test perfect fits with various sizes
  size_t sizes[] = {16, 32, 48, 64};
  std::vector<void *> ptrs;
  void *barrier;

  // Allocate blocks with barriers between them
  for (size_t sz : sizes) {
    void *p;
    ASSERT_NO_FATAL_FAILURE(checked_alloc(sz, &p));
    ptrs.push_back(p);
    ASSERT_NO_FATAL_FAILURE(checked_alloc(8, &barrier)); // Small barrier
  }

  // Free all the main blocks (not barriers)
  std::vector<void *> freed_ptrs;
  for (void *p : ptrs) {
    freed_ptrs.push_back(p);
    ASSERT_NO_FATAL_FAILURE(checked_free(p));
  }

  // Reallocate same sizes - should get perfect fits
  for (size_t i = 0; i < 4; i++) {
    void *p;
    ASSERT_NO_FATAL_FAILURE(checked_alloc(sizes[i], &p));
    // The allocator uses best-fit, so it should reuse the matching block
  }
}

// Coalescing tests
TEST_F(DPAllocatorTest, LeftCoalescing) {
  void *p1, *p2, *barrier;

  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p2));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(50, &barrier)); // Prevent right coalescing

  // Free p1 first (left block)
  ASSERT_NO_FATAL_FAILURE(checked_free(p1));

  // Count free blocks before freeing p2
  size_t free_blocks_before = 0;
  block_header *curr = allocator.free_list_head;
  while (curr) {
    free_blocks_before++;
    curr = curr->next;
  }

  // Free p2 (should coalesce left with p1)
  ASSERT_NO_FATAL_FAILURE(checked_free(p2));

  // Count free blocks after - should be same or fewer due to coalescing
  size_t free_blocks_after = 0;
  curr = allocator.free_list_head;
  while (curr) {
    free_blocks_after++;
    curr = curr->next;
  }

  // After coalescing, we should have fewer separate blocks
  ASSERT_LE(free_blocks_after, free_blocks_before);

  // Should be able to allocate a block that spans both freed regions
  void *large;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(180, &large));
}

TEST_F(DPAllocatorTest, RightCoalescing) {
  void *barrier, *p1, *p2;

  ASSERT_NO_FATAL_FAILURE(checked_alloc(50, &barrier)); // Prevent left coalescing
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(100, &p2));

  // Free p2 first (right block)
  ASSERT_NO_FATAL_FAILURE(checked_free(p2));

  size_t free_blocks_before = 0;
  block_header *curr = allocator.free_list_head;
  while (curr) {
    free_blocks_before++;
    curr = curr->next;
  }

  // Free p1 (should coalesce right with p2)
  ASSERT_NO_FATAL_FAILURE(checked_free(p1));

  size_t free_blocks_after = 0;
  curr = allocator.free_list_head;
  while (curr) {
    free_blocks_after++;
    curr = curr->next;
  }

  ASSERT_LE(free_blocks_after, free_blocks_before);

  // Should be able to allocate a block that spans both freed regions
  void *large;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(180, &large));
}

TEST_F(DPAllocatorTest, BothSidesCoalescing) {
  void *barrier_left, *p1, *p2, *p3, *barrier_right;

  ASSERT_NO_FATAL_FAILURE(checked_alloc(32, &barrier_left));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(80, &p1));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(80, &p2));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(80, &p3));
  ASSERT_NO_FATAL_FAILURE(checked_alloc(32, &barrier_right));

  // Free left and right neighbors first
  ASSERT_NO_FATAL_FAILURE(checked_free(p1));
  ASSERT_NO_FATAL_FAILURE(checked_free(p3));

  size_t free_blocks_before = 0;
  block_header *curr = allocator.free_list_head;
  while (curr) {
    free_blocks_before++;
    curr = curr->next;
  }

  // Free middle block - should coalesce with both neighbors
  ASSERT_NO_FATAL_FAILURE(checked_free(p2));

  size_t free_blocks_after = 0;
  curr = allocator.free_list_head;
  while (curr) {
    free_blocks_after++;
    curr = curr->next;
  }

  // Should have coalesced into fewer blocks
  ASSERT_LT(free_blocks_after, free_blocks_before);

  // Should be able to allocate a large block spanning all three
  void *large;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(220, &large));
}

TEST_F(DPAllocatorTest, CoalescingSequenceAlternating) {
  // Allocate many blocks
  std::vector<void *> ptrs;
  for (int i = 0; i < 8; i++) {
    void *p;
    ASSERT_NO_FATAL_FAILURE(checked_alloc(32, &p));
    ptrs.push_back(p);
  }

  // Free in alternating pattern: 0, 2, 4, 6
  for (int i = 0; i < 8; i += 2) {
    ASSERT_NO_FATAL_FAILURE(checked_free(ptrs[i]));
  }

  // Now free the remaining: 1, 3, 5, 7 - each should coalesce with neighbors
  for (int i = 1; i < 8; i += 2) {
    ASSERT_NO_FATAL_FAILURE(checked_free(ptrs[i]));
  }

  // After all frees, should have minimal fragmentation (one large block)
  size_t free_blocks = 0;
  block_header *curr = allocator.free_list_head;
  while (curr) {
    free_blocks++;
    curr = curr->next;
  }

  // All blocks should have coalesced
  ASSERT_EQ(free_blocks, 1);

  // Clear tracking since we already freed everything
  allocated.clear();

  // Should be able to allocate most of the buffer
  void *large;
  ASSERT_NO_FATAL_FAILURE(checked_alloc(800, &large));
}
