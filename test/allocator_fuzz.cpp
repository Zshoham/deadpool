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

// Structure to track allocations
struct AllocationRecord {
  void *ptr;
  size_t size;
  bool is_freed;
};

class AllocatorFuzzer {
private:
  static const size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer
  uint8_t buffer[BUFFER_SIZE];
  dp_alloc allocator;
  std::vector<AllocationRecord> allocations;

public:
  AllocatorFuzzer() { dp_init(&allocator, buffer, BUFFER_SIZE); }

  enum Operation {
    ALLOCATE,
    FREE,
    FREE_RANDOM,
    REALLOC_EQUIVALENT, // Free + Malloc of same size
    WRITE_TO_MEMORY,
    CORRUPT_HEADER // For testing robustness
  };

  bool performOperation(Operation op, size_t size, size_t index) {
    switch (op) {
    case ALLOCATE: {
      void *ptr = dp_malloc(&allocator, size);
      if (ptr) {
        allocations.push_back({ptr, size, false});
        // Write a pattern to the allocated memory
        std::memset(ptr, 0xAA, size);
      }
      return ptr != nullptr;
    }

    case FREE: {
      if (index >= allocations.size())
        return false;
      if (!allocations[index].is_freed) {
        dp_free(&allocator, allocations[index].ptr);
        allocations[index].is_freed = true;
      }
      return true;
    }

    case FREE_RANDOM: {
      if (allocations.empty())
        return false;
      size_t rand_index = index % allocations.size();
      if (!allocations[rand_index].is_freed) {
        dp_free(&allocator, allocations[rand_index].ptr);
        allocations[rand_index].is_freed = true;
      }
      return true;
    }

    case REALLOC_EQUIVALENT: {
      if (index >= allocations.size())
        return false;
      if (allocations[index].is_freed)
        return false;

      void *new_ptr = dp_malloc(&allocator, allocations[index].size);
      if (new_ptr) {
        std::memcpy(new_ptr, allocations[index].ptr, allocations[index].size);
        dp_free(&allocator, allocations[index].ptr);
        allocations[index].ptr = new_ptr;
      }
      return new_ptr != nullptr;
    }

    case WRITE_TO_MEMORY: {
      if (index >= allocations.size())
        return false;
      if (allocations[index].is_freed)
        return false;

      // Write a test pattern to the memory
      std::memset(allocations[index].ptr, 0xBB, allocations[index].size);
      return true;
    }

    case CORRUPT_HEADER: {
      if (index >= allocations.size())
        return false;
      if (allocations[index].is_freed)
        return false;

      // Attempt to corrupt the header (for testing robustness)
      uint8_t *header_ptr =
          static_cast<uint8_t *>(allocations[index].ptr) - sizeof(block_header);
      header_ptr[0] ^= 0xFF; // Flip bits in the header
      return true;
    }
    }
    return false;
  }

  void cleanup() {
    for (auto &alloc : allocations) {
      if (!alloc.is_freed) {
        dp_free(&allocator, alloc.ptr);
      }
    }
    allocations.clear();
  }

  ~AllocatorFuzzer() { cleanup(); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 4)
    return 0; // Need minimum input size

  FuzzedDataProvider provider(Data, Size);
  AllocatorFuzzer fuzzer;

  // Number of operations to perform
  const uint32_t num_ops = provider.ConsumeIntegralInRange<uint32_t>(1, 1000);

  for (uint32_t i = 0; i < num_ops && provider.remaining_bytes() > 0; i++) {
    // Get random operation
    AllocatorFuzzer::Operation op = static_cast<AllocatorFuzzer::Operation>(
        provider.ConsumeIntegralInRange<uint8_t>(0, 5));

    // Get random size (weighted towards interesting values)
    size_t alloc_size;
    if (provider.ConsumeBool()) {
      // Use common sizes
      alloc_size = provider.PickValueInArray<size_t>(
          {0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, sizeof(void *),
           sizeof(void *) - 1, sizeof(void *) + 1});
    } else {
      // Use random size
      alloc_size = provider.ConsumeIntegralInRange<size_t>(0, 16384);
    }

    // Get random index
    size_t index = provider.ConsumeIntegralInRange<size_t>(0, 1000);

    // Perform operation
    fuzzer.performOperation(op, alloc_size, index);
  }

  return 0;
}

// Custom mutator to generate more interesting inputs
// extern "C" size_t LLVMFuzzerCustomMutator(uint8_t *Data, size_t Size,
//                                           size_t MaxSize, unsigned int Seed) {
//   // Create a new provider with the seed
//   std::mt19937 gen(Seed);
//
//   // Generate structured input
//   std::vector<uint8_t> new_data;
//
//   // Number of operations
//   uint32_t num_ops = std::uniform_int_distribution<uint32_t>(1, 100)(gen);
//   new_data.insert(new_data.end(), reinterpret_cast<uint8_t *>(&num_ops),
//                   reinterpret_cast<uint8_t *>(&num_ops) + sizeof(num_ops));
//
//   // Generate operations
//   for (uint32_t i = 0; i < num_ops && new_data.size() < MaxSize - 8; i++) {
//     // Operation type
//     uint8_t op = std::uniform_int_distribution<uint8_t>(0, 5)(gen);
//     new_data.push_back(op);
//
//     // Size (occasionally use interesting values)
//     uint32_t size;
//     if (gen() % 4 == 0) {
//       static const uint32_t interesting_sizes[] = {
//           0,
//           1,
//           2,
//           4,
//           8,
//           16,
//           32,
//           64,
//           128,
//           256,
//           512,
//           1024,
//           static_cast<uint32_t>(sizeof(void *)),
//           static_cast<uint32_t>(sizeof(void *) - 1),
//           static_cast<uint32_t>(sizeof(void *) + 1)};
//       size = interesting_sizes[gen() % sizeof(interesting_sizes) /
//                                sizeof(interesting_sizes[0])];
//     } else {
//       size = std::uniform_int_distribution<uint32_t>(0, 16384)(gen);
//     }
//     new_data.insert(new_data.end(), reinterpret_cast<uint8_t *>(&size),
//                     reinterpret_cast<uint8_t *>(&size) + sizeof(size));
//   }
//
//   // Copy to output buffer
//   Size = std::min(MaxSize, new_data.size());
//   std::memcpy(Data, new_data.data(), Size);
//   return Size;
// }
//
// // Custom cross-over to combine interesting test cases
// extern "C" size_t LLVMFuzzerCustomCrossOver(const uint8_t *Data1, size_t Size1,
//                                             const uint8_t *Data2, size_t Size2,
//                                             uint8_t *Out, size_t MaxOutSize,
//                                             unsigned int Seed) {
//   std::mt19937 gen(Seed);
//
//   // Read number of operations from both inputs
//   uint32_t num_ops1 = 0, num_ops2 = 0;
//   if (Size1 >= sizeof(uint32_t))
//     std::memcpy(&num_ops1, Data1, sizeof(uint32_t));
//   if (Size2 >= sizeof(uint32_t))
//     std::memcpy(&num_ops2, Data2, sizeof(uint32_t));
//
//   // Create new sequence combining both inputs
//   std::vector<uint8_t> new_data;
//   uint32_t new_num_ops =
//       std::min(num_ops1 + num_ops2, static_cast<uint32_t>(MaxOutSize / 8));
//
//   new_data.insert(new_data.end(), reinterpret_cast<uint8_t *>(&new_num_ops),
//                   reinterpret_cast<uint8_t *>(&new_num_ops) +
//                       sizeof(new_num_ops));
//
//   // Randomly select operations from both inputs
//   size_t pos1 = sizeof(uint32_t), pos2 = sizeof(uint32_t);
//   while (new_data.size() < MaxOutSize - 8 && (pos1 < Size1 || pos2 < Size2)) {
//
//     // Choose which input to take from
//     if (pos1 >= Size1)
//       pos2 += 5;
//     else if (pos2 >= Size2)
//       pos1 += 5;
//     else if (gen() % 2)
//       pos1 += 5;
//     else
//       pos2 += 5;
//
//     // Copy operation and its data
//     if (pos1 < Size1) {
//       new_data.insert(new_data.end(), Data1 + pos1 - 5, Data1 + pos1);
//     } else if (pos2 < Size2) {
//       new_data.insert(new_data.end(), Data2 + pos2 - 5, Data2 + pos2);
//     }
//   }
//
//   // Copy to output buffer
//   size_t OutSize = std::min(MaxOutSize, new_data.size());
//   std::memcpy(Out, new_data.data(), OutSize);
//   return OutSize;
// }
