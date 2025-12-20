#include "test_common.hpp"

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
  ASSERT_EQ(allocated.end(), std::adjacent_find(allocated.begin(), allocated.end()));
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
