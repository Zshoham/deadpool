#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "allocator.h"

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
    dp_init(&allocator, buffer.data(), BUFFER_SIZE);
  }

  size_t available() {
    auto totoal_used = 0;
    for (auto allocation : allocated) {
      totoal_used += sizeof(block_header) + allocation.size;
    }
    return BUFFER_SIZE - (totoal_used + sizeof(block_header));
  }

  void checked_alloc(size_t alloc_size, void** result) {
    *result = dp_malloc(&allocator, alloc_size);
    allocated.push_back({*result, alloc_size});
    ASSERT_NE(nullptr, *result);
    ASSERT_NE(allocator.free_list_head, nullptr);
    EXPECT_TRUE(allocator.free_list_head->is_free);
    ASSERT_GE(allocator.available, available());
  }

  void checked_free(void *ptr) {
    auto to_remove =
        std::find_if(allocated.begin(), allocated.end(),
                     [&](auto allocation) { return allocation.ptr == ptr; });
    ASSERT_NE(to_remove, allocated.end());
    ASSERT_EQ(dp_free(&allocator, to_remove->ptr), 0);
    allocated.erase(to_remove);
  }

  void TearDown() override {
    for (auto allocation : allocated) {
      ASSERT_EQ(dp_free(&allocator, allocation.ptr), 0);
    }
    allocated.clear();
  }
};

// Basic Allocation Tests
TEST_F(DPAllocatorTest, SingleAllocation) {
  void *result;
  checked_alloc(100, &result);
}

TEST_F(DPAllocatorTest, MultipleAllocations) {
  size_t allocated_size = 0;
  void *ptr;
  for (int i = 0; i < 5; i++) {
    checked_alloc(100, &ptr); 
  }

  // Verify all pointers are different
  std::sort(allocated.begin(), allocated.end());
  EXPECT_EQ(allocated.end(), std::adjacent_find(allocated.begin(), allocated.end()));
}

// Fragmentation Tests
TEST_F(DPAllocatorTest, FragmentationAndCoalescing) {
  void *ptr1, *ptr2, *ptr3, *ptr4;
  checked_alloc(100, &ptr1);
  checked_alloc(100, &ptr2);
  checked_alloc(100, &ptr3);

  // Create fragmentation by freeing middle block
  checked_free(ptr2);

  // Allocate slightly smaller block - should fit in the gap
  checked_alloc(100, &ptr4);
  block_header *p4_block =
        (block_header *)((uint8_t *)ptr4 - sizeof(block_header));


  // Free all blocks
  checked_free(ptr1);
  checked_free(ptr3);
  checked_free(ptr4);

  // Should be able to allocate a large block now
  void *large_ptr;
  checked_alloc(900, &large_ptr);
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
  void *ptr;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    checked_alloc(ALLOC_SIZE, &ptr);
    checked_free(ptr);
  }
}

TEST_F(DPAllocatorTest, RandomizedAllocationsAndFrees) {
  const int NUM_ALLOCS = 20;
  void *ptr;

  // Random allocations
  for (int i = 0; i < NUM_ALLOCS; i++) {
    size_t size = rand() % 64 + 1; // Random size between 1 and 64
    checked_alloc(size, &ptr);
  }

  // Random frees
  while (!allocated.empty()) {
    size_t index = rand() % allocated.size();
    checked_free(allocated[index].ptr);
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
