#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <iostream>

#include "FuzzedDataProvider.hpp"
#include "allocator.h"

// Structure to track allocations made by the fuzzer.
// This allows the fuzzer to perform operations (like free, realloc, corrupt)
// on previously allocated blocks.
struct AllocationRecord {
  void *ptr;      // Pointer to the allocated memory block (user data area)
  size_t size;    // Size of the user data area
  bool is_freed;  // Flag to indicate if the block has been freed
};

// AllocatorFuzzer class encapsulates the allocator instance, buffer,
// and fuzzing logic. It maintains a list of active allocations
// to perform various operations on them.
class AllocatorFuzzer {
private:
  static const size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer for the allocator
  uint8_t buffer[BUFFER_SIZE]; // The actual memory arena for the allocator
  dp_alloc allocator;          // The allocator instance being fuzzed
  std::vector<AllocationRecord> allocations; // Tracks active allocations

public:
  // Constructor: Initializes the allocator with the buffer.
  AllocatorFuzzer() { dp_init(&allocator, buffer, BUFFER_SIZE); }

  // Defines the set of operations the fuzzer can perform.
  enum Operation {
    ALLOCATE,           // Allocate memory
    FREE,               // Free a specific previously allocated block
    FREE_RANDOM,        // Free a randomly chosen previously allocated block
    REALLOC_EQUIVALENT, // Simulate realloc by freeing then mallocing with the same size
    WRITE_TO_MEMORY,    // Write a pattern to an allocated block
    CORRUPT_HEADER      // Corrupt the header of an allocated block to test robustness
  };

  // Executes a given operation.
  // `op`: The operation to perform.
  // `size`: Size parameter, primarily used for ALLOCATE.
  // `index`: Index parameter, used to select a block from `allocations` for FREE, REALLOC, WRITE, CORRUPT.
  bool performOperation(Operation op, size_t size, size_t index) {
    switch (op) {
    case ALLOCATE: {
      // Attempt to allocate memory of 'size'.
      // If successful, record the allocation and write a pattern to it.
      void *ptr = dp_malloc(&allocator, size);
      if (ptr) {
        allocations.push_back({ptr, size, false});
        // Write a known pattern to the allocated memory to check for overwrites later (implicitly).
        std::memset(ptr, 0xAA, size);
      }
      return ptr != nullptr;
    }

    case FREE: {
      // Free a specific allocation record if it exists and is not already freed.
      if (index >= allocations.size())
        return false; // Index out of bounds
      if (!allocations[index].is_freed) {
        dp_free(&allocator, allocations[index].ptr);
        allocations[index].is_freed = true;
      }
      return true;
    }

    case FREE_RANDOM: {
      // Free a randomly selected, currently active allocation.
      if (allocations.empty())
        return false;
      size_t rand_index = index % allocations.size(); // Use provided index to pick a "random" block
      if (!allocations[rand_index].is_freed) {
        dp_free(&allocator, allocations[rand_index].ptr);
        allocations[rand_index].is_freed = true;
      }
      return true;
    }

    case REALLOC_EQUIVALENT: {
      // Simulate realloc: free an existing allocation and then malloc a new one of the same size.
      // This tests the allocator's ability to reuse freed blocks.
      if (index >= allocations.size())
        return false;
      if (allocations[index].is_freed)
        return false; // Cannot "realloc" a freed block

      void *old_ptr = allocations[index].ptr;
      size_t old_size = allocations[index].size;
      void *new_ptr = dp_malloc(&allocator, old_size);

      if (new_ptr) {
        // If new allocation succeeds, copy data from old block and then free the old block.
        // The size of data to copy should be min(old_size, new_size), but here they are the same.
        std::memcpy(new_ptr, old_ptr, old_size);
        dp_free(&allocator, old_ptr);
        // Update the record to point to the new block.
        allocations[index].ptr = new_ptr;
        // allocations[index].is_freed remains false as it's a new valid allocation.
      }
      return new_ptr != nullptr;
    }

    case WRITE_TO_MEMORY: {
      // Write a specific pattern to an active allocation.
      // This helps test if memory is corrupted by other operations or if reads/writes are correct.
      if (index >= allocations.size())
        return false;
      if (allocations[index].is_freed)
        return false;

      std::memset(allocations[index].ptr, 0xBB, allocations[index].size);
      return true;
    }

    case CORRUPT_HEADER: {
      // Attempt to corrupt the header of an active allocation to test allocator robustness.
      // This simulates a scenario where metadata is damaged.
      if (index >= allocations.size())
        return false;
      if (allocations[index].is_freed)
        return false; // Cannot corrupt header of a freed block via this record

      // Locate the header (just before the user data pointer)
      block_header *header = (block_header *)((uint8_t *)allocations[index].ptr - sizeof(block_header));
      
      // Corrupt by flipping bits in the first byte of the header.
      // This is a simple form of corruption. Depending on which bits are flipped,
      // it might alter the 'size', 'is_free' status, or parts of the 'next' pointer.
      // The allocator's dp_free (or other operations) might detect this if:
      // - The `is_free` flag is unexpectedly true (double free).
      // - The `next` pointer (used by dp_malloc to identify allocated blocks) is UINTPTR_MAX,
      //   but other fields make it look invalid upon freeing.
      // - The size is corrupted leading to issues during coalescing or free list management.
      // This test is not exhaustive for all corruption types but checks basic resilience.
      // A robust allocator should ideally not crash and might return an error on operations
      // involving such a corrupted block (e.g., when dp_free is eventually called).
      ((uint8_t*)header)[0] ^= 0xFF; 
      return true;
    }
    }
    return false; // Should not be reached if all ops are handled
  }

  // cleanup() is called to free any remaining active allocations.
  // This is important for fuzzing, as LLVMFuzzerTestOneInput might end
  // without all allocations being explicitly freed by the fuzzing operations.
  void cleanup() {
    for (auto &alloc : allocations) {
      if (!alloc.is_freed) {
        dp_free(&allocator, alloc.ptr);
      }
    }
    allocations.clear();
  }

  // Destructor: ensures cleanup is called.
  ~AllocatorFuzzer() { cleanup(); }
};

// Main fuzzing entry point called by the libFuzzer engine.
// The fuzzer performs a sequence of allocator operations based on the input data.
// Its main goal is to find sequences of operations that cause crashes (e.g., segmentation faults),
// assertion failures, or other undefined behavior, indicating bugs in the allocator.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 4) // Require a minimum amount of data to define number of operations.
    return 0; 

  FuzzedDataProvider provider(Data, Size); // Helper to consume data from the input buffer.
  AllocatorFuzzer fuzzer; // Create an instance of our fuzzer class.

  // Determine the number of operations to perform in this fuzzing iteration.
  const uint32_t num_ops = provider.ConsumeIntegralInRange<uint32_t>(1, 1000);

  for (uint32_t i = 0; i < num_ops && provider.remaining_bytes() > 0; i++) {
    // Consume data to decide which operation to perform.
    AllocatorFuzzer::Operation op = static_cast<AllocatorFuzzer::Operation>(
        provider.ConsumeIntegralInRange<uint8_t>(0, 5)); // Max value is last enum member

    // Consume data for the size of the allocation.
    // Mixes common, specific sizes with a range of random sizes.
    size_t alloc_size;
    if (provider.ConsumeBool()) {
      // Pick from a list of potentially interesting/edge-case sizes.
      alloc_size = provider.PickValueInArray<size_t>(
          {0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, sizeof(void *),
           sizeof(void *) - 1, sizeof(void *) + 1});
    } else {
      // Pick a random size within a broader range.
      alloc_size = provider.ConsumeIntegralInRange<size_t>(0, 16384); // Up to 16KB
    }

    // Consume data for an index, used by operations like FREE, REALLOC_EQUIVALENT, etc.
    // The performOperation method will handle this index (e.g., by using modulo).
    size_t index = provider.ConsumeIntegralInRange<size_t>(0, 1000);

    // Execute the chosen operation.
    fuzzer.performOperation(op, alloc_size, index);
  }

  // The fuzzer's destructor will call cleanup() to free any remaining allocations.
  return 0; // Indicates successful execution of the fuzz test case.
}
