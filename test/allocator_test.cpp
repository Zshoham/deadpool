#include "allocator.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <vector>

class DPAllocatorTest : public ::testing::Test {
protected:
  static constexpr size_t BUFFER_SIZE = 1024;
  std::array<uint8_t, BUFFER_SIZE> buffer;
  dp_alloc allocator;
  std::vector<void*> allocated;

  void SetUp() override {
    buffer.fill(0);
    allocated.clear(); // Ensure allocated is empty for each test
    dp_init(&allocator, buffer.data(), BUFFER_SIZE);
  }

  void TearDown() override {
    for (auto ptr : allocated) {
      // It's okay if dp_free is called on an already freed pointer here,
      // as some tests might free memory themselves. dp_free should handle it.
      // For double free tests, dp_free will return an error, but we don't check return here.
      dp_free(&allocator, ptr);
    }
    // allocated is cleared in SetUp() for each test to ensure a clean state.
  }
};

// Basic Allocation Tests
TEST_F(DPAllocatorTest, SingleAllocation) {
  size_t alloc_size = 100;
  void *ptr = dp_malloc(&allocator, alloc_size);
  ASSERT_NE(nullptr, ptr);
  allocated.push_back(ptr); // Add to vector for TearDown

  // After one allocation, the free list head should not be null (unless buffer was tiny)
  ASSERT_NE(nullptr, allocator.free_list_head);
  EXPECT_TRUE(allocator.free_list_head->is_free);

  // Expected available space: total buffer - one header for initial free block - allocated user data - header for allocated block
  // However, initial free block's header is "converted" to allocated block's header.
  // If the block is split:
  // Initial available payload: BUFFER_SIZE - sizeof(block_header)
  // Allocated: alloc_size (user data) + sizeof(block_header) (for its header)
  // Remaining available payload: (BUFFER_SIZE - sizeof(block_header)) - (alloc_size + sizeof(block_header))
  // The new free_list_head will point to this remaining payload.
  size_t expected_available_payload = BUFFER_SIZE - sizeof(block_header) - (alloc_size + sizeof(block_header));
  // The allocator.available field should track the total payload available in all free blocks.
  EXPECT_EQ(allocator.available, expected_available_payload);
  ASSERT_NE(nullptr, allocator.free_list_head); // Should exist if space left
  EXPECT_EQ(allocator.free_list_head->size, expected_available_payload);
  // dp_free(&allocator, ptr); // This would normally be here or in TearDown. Handled by TearDown.
}

TEST_F(DPAllocatorTest, MultipleAllocations) {
  std::vector<void *> local_ptrs; // Keep track of pointers for this test
  size_t total_payload_allocated_by_test = 0;
  size_t initial_available_payload = allocator.available; // BUFFER_SIZE - sizeof(block_header)

  for (int i = 0; i < 5; i++) {
    size_t current_alloc_size = 100;
    void *ptr = dp_malloc(&allocator, current_alloc_size);
    ASSERT_NE(nullptr, ptr);
    local_ptrs.push_back(ptr);
    // this->allocated.push_back(ptr); // Add to global for TearDown, if not freed locally

    // Each allocation consumes user_size + sizeof(block_header) from available payload
    total_payload_allocated_by_test += current_alloc_size + sizeof(block_header);
    EXPECT_EQ(allocator.available, initial_available_payload - total_payload_allocated_by_test);
  }

  // Verify all pointers are different
  std::vector<void *> sorted_ptrs = local_ptrs;
  std::sort(sorted_ptrs.begin(), sorted_ptrs.end());
  EXPECT_EQ(std::adjacent_find(sorted_ptrs.begin(), sorted_ptrs.end()), sorted_ptrs.end());

  for (auto ptr : local_ptrs) {
    ASSERT_EQ(0, dp_free(&allocator, ptr));
  }
  local_ptrs.clear(); // Clear local tracking

  // After freeing all, available should be back to initial (or very close, depending on coalescing behavior)
  // With perfect coalescing, it should be exactly the initial available payload.
  EXPECT_EQ(allocator.available, initial_available_payload);
  ASSERT_NE(nullptr, allocator.free_list_head);
  EXPECT_EQ(allocator.free_list_head->size, initial_available_payload);
}

// Fragmentation Tests
TEST_F(DPAllocatorTest, FragmentationAndCoalescing) {
  void *ptr1 = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr1);
  allocated.push_back(ptr1);

  void *ptr2 = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr2);
  // Not adding ptr2 to allocated as it's freed mid-test

  void *ptr3 = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr3);
  allocated.push_back(ptr3);

  // State before freeing ptr2
  block_header* header_ptr2 = (block_header*)((uint8_t*)ptr2 - sizeof(block_header));
  size_t size_ptr2 = header_ptr2->size; // User payload size

  // Create fragmentation by freeing middle block
  ASSERT_EQ(0, dp_free(&allocator, ptr2)); // Expect successful free

  // After freeing ptr2, its block should be the head of the free list (or coalesced)
  // For simplicity, let's assume no coalescing with initial/final buffer boundaries here if other blocks exist.
  // The freed block of ptr2 should be available.
  // The current allocator coalesces and adds to the front.
  // Let's check if the free list head IS the block of ptr2.
  ASSERT_NE(nullptr, allocator.free_list_head);
  EXPECT_EQ(allocator.free_list_head, header_ptr2);
  EXPECT_TRUE(allocator.free_list_head->is_free);
  EXPECT_EQ(allocator.free_list_head->size, size_ptr2); // Should be original payload size

  // Allocate slightly smaller block - should fit in the gap
  // The allocator should reuse the block previously occupied by ptr2.
  void *ptr4 = dp_malloc(&allocator, 100); // Same size as ptr2
  ASSERT_NE(nullptr, ptr4);
  allocated.push_back(ptr4);
  EXPECT_EQ(ptr4, ptr2); // Expect ptr4 to reuse the memory of ptr2

  // Free all blocks (ptr2 already freed)
  // dp_free in TearDown will handle ptr1, ptr3, ptr4

  // Should be able to allocate a large block now after TearDown implicitly frees all
  // This part of the test is better suited for a separate test or after explicit frees.
  // For now, let's ensure TearDown cleans up.
  // To test large allocation, free them explicitly here.
  dp_free(&allocator, ptr1); allocated.erase(std::remove(allocated.begin(), allocated.end(), ptr1), allocated.end());
  dp_free(&allocator, ptr3); allocated.erase(std::remove(allocated.begin(), allocated.end(), ptr3), allocated.end());
  dp_free(&allocator, ptr4); allocated.erase(std::remove(allocated.begin(), allocated.end(), ptr4), allocated.end());


  void *large_ptr = dp_malloc(&allocator, BUFFER_SIZE - sizeof(block_header)); // Max possible
  ASSERT_NE(nullptr, large_ptr);
  allocated.push_back(large_ptr);
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
  // The total usable payload in the buffer initially is BUFFER_SIZE - sizeof(block_header)
  size_t exact_alloc_size = BUFFER_SIZE - sizeof(block_header);
  void *ptr = dp_malloc(&allocator, exact_alloc_size);
  ASSERT_NE(nullptr, ptr);
  allocated.push_back(ptr);

  // After allocating the exact available size, the free list should be empty.
  EXPECT_EQ(nullptr, allocator.free_list_head);
  EXPECT_EQ(allocator.available, 0);

  // Any further allocation should fail
  EXPECT_EQ(nullptr, dp_malloc(&allocator, 1));
}

// Alignment Tests
TEST_F(DPAllocatorTest, PointerAlignment) {
  void *ptr = dp_malloc(&allocator, 1);
  ASSERT_NE(nullptr, ptr);
  allocated.push_back(ptr);
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(ptr) % sizeof(void *));
  // The allocator does not guarantee alignment beyond what malloc naturally provides or block_header structure dictates.
  // A more robust alignment test would require the allocator to support explicit alignment requests.
  // For now, this checks natural alignment.
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
  // ptr is not added to this->allocated because we are testing dp_free behavior here.
  // If it were added, TearDown would call dp_free again.

  ASSERT_EQ(0, dp_free(&allocator, ptr)); // First free should succeed

  size_t available_before_double_free = allocator.available;
  // Second free should fail (return non-zero)
  EXPECT_NE(0, dp_free(&allocator, ptr)); 

  // Available memory should not change after a failed double free
  EXPECT_EQ(allocator.available, available_before_double_free);

  // Should still be able to allocate if space was indeed freed correctly the first time
  // and not corrupted by the double free attempt.
  // To make this robust, we need to know the state of 'allocator.available' after the first free.
  // Let's re-allocate the same block to see if the space is usable.
  // However, the original ptr's block is now in free_list_head.
  // If we dp_malloc(100) again, we should get the same ptr.
  
  // Reset allocator state for a clean check, or ensure this test is self-contained for this check.
  // The above EXPECT_NE already tests dp_free's return.
  // The following checks if the allocator state is still valid.
  void *new_ptr = dp_malloc(&allocator, 100); // Attempt to allocate
  ASSERT_NE(nullptr, new_ptr); // Should succeed if first free worked
  allocated.push_back(new_ptr); // For TearDown
  // EXPECT_EQ(new_ptr, ptr); // It might re-allocate the same memory spot, but not guaranteed.
                           // What's important is that a valid block is allocated.
}

TEST_F(DPAllocatorTest, FreeNullPtr) {
  // dp_free should handle nullptr gracefully (return 0, no crash)
  EXPECT_EQ(0, dp_free(&allocator, nullptr));
  size_t available_before = allocator.available;
  EXPECT_EQ(0, dp_free(&allocator, nullptr)); // Call again to be sure
  EXPECT_EQ(allocator.available, available_before); // Available memory should not change
}
