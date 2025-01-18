#include "allocator.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <vector>

class DPAllocatorTest : public ::testing::Test {
protected:
  static constexpr size_t BUFFER_SIZE = 1024;
  std::array<uint8_t, BUFFER_SIZE> buffer;
  dp_alloc allocator;

  void SetUp() override {
    buffer.fill(0);
    dp_init(&allocator, buffer.data(), BUFFER_SIZE);
  }
};

// Basic Allocation Tests
TEST_F(DPAllocatorTest, SingleAllocation) {
  void *ptr = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr);
  EXPECT_GE(allocator.available, BUFFER_SIZE - 100 - 2 * sizeof(block_header));
}

TEST_F(DPAllocatorTest, MultipleAllocations) {
  std::vector<void *> ptrs;
  for (int i = 0; i < 5; i++) {
    void *ptr = dp_malloc(&allocator, 100);
    ASSERT_NE(nullptr, ptr);
    ptrs.push_back(ptr);
  }

  // Verify all pointers are different
  std::sort(ptrs.begin(), ptrs.end());
  EXPECT_EQ(ptrs.end(), std::adjacent_find(ptrs.begin(), ptrs.end()));
}

// Fragmentation Tests
TEST_F(DPAllocatorTest, FragmentationAndCoalescing) {
  void *ptr1 = dp_malloc(&allocator, 100);
  void *ptr2 = dp_malloc(&allocator, 100);
  void *ptr3 = dp_malloc(&allocator, 100);

  // Create fragmentation by freeing middle block
  dp_free(&allocator, ptr2);

  // Allocate slightly smaller block - should fit in the gap
  void *ptr4 = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr4);

  // Free all blocks
  dp_free(&allocator, ptr1);
  dp_free(&allocator, ptr3);
  dp_free(&allocator, ptr4);

  // Should be able to allocate a large block now
  void *large_ptr =
      dp_malloc(&allocator, 900);
  ASSERT_NE(nullptr, large_ptr);
}

// Edge Cases
TEST_F(DPAllocatorTest, ZeroSizeAllocation) {
  void *ptr = dp_malloc(&allocator, 0);
  EXPECT_EQ(nullptr, ptr);
}

TEST_F(DPAllocatorTest, TooLargeAllocation) {
  void *ptr = dp_malloc(&allocator, BUFFER_SIZE + 1);
  EXPECT_EQ(nullptr, ptr);
}

TEST_F(DPAllocatorTest, ExactSizeAllocation) {
  void *ptr = dp_malloc(&allocator, BUFFER_SIZE - 2 * sizeof(block_header));
  ASSERT_NE(nullptr, ptr);
  EXPECT_EQ(nullptr, dp_malloc(&allocator, 1)); // Should be full
}

// Alignment Tests
TEST_F(DPAllocatorTest, PointerAlignment) {
  void *ptr = dp_malloc(&allocator, 1);
  ASSERT_NE(nullptr, ptr);
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(ptr) % sizeof(void *));
}

// Stress Tests
TEST_F(DPAllocatorTest, AlternatingAllocationFreeing) {
  const int NUM_ITERATIONS = 100;
  const size_t ALLOC_SIZE = 16;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    void *ptr = dp_malloc(&allocator, ALLOC_SIZE);
    ASSERT_NE(nullptr, ptr);
    dp_free(&allocator, ptr);
  }
}

TEST_F(DPAllocatorTest, RandomizedAllocationsAndFrees) {
  std::vector<void *> ptrs;
  const int NUM_ALLOCS = 20;

  // Random allocations
  for (int i = 0; i < NUM_ALLOCS; i++) {
    size_t size = rand() % 64 + 1; // Random size between 1 and 64
    void *ptr = dp_malloc(&allocator, size);
    if (ptr)
      ptrs.push_back(ptr);
  }

  // Random frees
  while (!ptrs.empty()) {
    size_t index = rand() % ptrs.size();
    dp_free(&allocator, ptrs[index]);
    ptrs.erase(ptrs.begin() + index);
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
