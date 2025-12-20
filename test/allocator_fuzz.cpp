#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include "allocator.h"
#include "config_macros.h"

namespace {

static constexpr size_t BUFFER_SIZE = 4096;
static constexpr size_t DEFAULT_ALIGN = alignof(max_align_t);

inline void noop_log(const char *, ...) {}

class AllocatorFixture {
public:
  AllocatorFixture() {
    buffer_.resize(BUFFER_SIZE, 0);
    dp_init(&allocator_, buffer_.data(),
            BUFFER_SIZE IF_DP_LOG(, {.debug = noop_log,
                                     .info = noop_log,
                                     .warning = noop_log,
                                     .error = noop_log}));
  }

  dp_alloc *get() { return &allocator_; }

private:
  std::vector<uint8_t> buffer_;
  dp_alloc allocator_;
};

void SingleAllocationDoesNotCrash(size_t size) {
  if (size > BUFFER_SIZE)
    return;

  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  void *ptr = dp_malloc(alloc, size);
  if (ptr != nullptr) {
    if (size > 0) {
      memset(ptr, 0xAB, size);
    }
    EXPECT_EQ(dp_free(alloc, ptr), 0);
  }
}
FUZZ_TEST(AllocatorFuzzTest, SingleAllocationDoesNotCrash)
    .WithDomains(fuzztest::InRange<size_t>(0, BUFFER_SIZE * 2));

void AllocationSequenceDoesNotCrash(const std::vector<size_t> &sizes) {
  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  std::vector<void *> ptrs;
  for (size_t size : sizes) {
    if (size > 512)
      continue;

    void *ptr = dp_malloc(alloc, size);
    if (ptr != nullptr) {
      if (size > 0) {
        memset(ptr, 0xCD, size);
      }
      ptrs.push_back(ptr);
    }
  }

  for (void *ptr : ptrs) {
    EXPECT_EQ(dp_free(alloc, ptr), 0);
  }
}
FUZZ_TEST(AllocatorFuzzTest, AllocationSequenceDoesNotCrash)
    .WithDomains(fuzztest::VectorOf(fuzztest::InRange<size_t>(0, 1024))
                     .WithMaxSize(100));

void AllocFreeInterleavedDoesNotCrash(
    const std::vector<std::pair<bool, size_t>> &operations) {
  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  std::vector<void *> live;

  for (const auto &[is_alloc, value] : operations) {
    if (is_alloc) {
      size_t size = value % 256;
      void *ptr = dp_malloc(alloc, size);
      if (ptr != nullptr) {
        if (size > 0) {
          memset(ptr, 0xEE, size);
        }
        live.push_back(ptr);
      }
    } else if (!live.empty()) {
      size_t idx = value % live.size();
      EXPECT_EQ(dp_free(alloc, live[idx]), 0);
      live.erase(live.begin() + static_cast<long>(idx));
    }
  }

  for (void *ptr : live) {
    EXPECT_EQ(dp_free(alloc, ptr), 0);
  }
}
FUZZ_TEST(AllocatorFuzzTest, AllocFreeInterleavedDoesNotCrash)
    .WithDomains(
        fuzztest::VectorOf(fuzztest::PairOf(fuzztest::Arbitrary<bool>(),
                                            fuzztest::Arbitrary<size_t>()))
            .WithMaxSize(200));

void MemoryContentsPreserved(const std::vector<std::pair<size_t, uint8_t>> &allocs) {
  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  struct TrackedAlloc {
    void *ptr;
    size_t size;
    uint8_t pattern;
  };
  std::vector<TrackedAlloc> live;

  for (const auto &[raw_size, pattern] : allocs) {
    size_t size = (raw_size % 128) + 1;
    void *ptr = dp_malloc(alloc, size);
    if (ptr != nullptr) {
      memset(ptr, pattern, size);
      live.push_back({ptr, size, pattern});
    }
  }

  for (const auto &tracked : live) {
    const uint8_t *bytes = static_cast<const uint8_t *>(tracked.ptr);
    for (size_t i = 0; i < tracked.size; ++i) {
      EXPECT_EQ(bytes[i], tracked.pattern) << "Memory corruption at byte " << i;
    }
    EXPECT_EQ(dp_free(alloc, tracked.ptr), 0);
  }
}
FUZZ_TEST(AllocatorFuzzTest, MemoryContentsPreserved)
    .WithDomains(fuzztest::VectorOf(fuzztest::PairOf(
                                        fuzztest::Arbitrary<size_t>(),
                                        fuzztest::Arbitrary<uint8_t>()))
                     .WithMaxSize(50));

void AlignmentIsCorrect(size_t size) {
  if (size == 0 || size > BUFFER_SIZE / 2)
    return;

  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  void *ptr = dp_malloc(alloc, size);
  if (ptr != nullptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % DEFAULT_ALIGN, 0u) << "Misaligned pointer for size " << size;
    EXPECT_EQ(dp_free(alloc, ptr), 0);
  }
}
FUZZ_TEST(AllocatorFuzzTest, AlignmentIsCorrect)
    .WithDomains(fuzztest::InRange<size_t>(1, BUFFER_SIZE));

void CoalescingWorks(const std::vector<size_t> &sizes,
                     const std::vector<size_t> &free_order_indices) {
  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  std::vector<void *> ptrs;
  for (size_t raw_size : sizes) {
    size_t size = (raw_size % 64) + 1;
    void *ptr = dp_malloc(alloc, size);
    if (ptr != nullptr) {
      memset(ptr, 0xFF, size);
      ptrs.push_back(ptr);
    }
    if (ptrs.size() >= 20)
      break;
  }

  if (ptrs.empty())
    return;

  std::vector<size_t> order;
  for (size_t i = 0; i < ptrs.size(); ++i) {
    order.push_back(i);
  }

  for (size_t idx : free_order_indices) {
    if (order.size() < 2)
      break;
    size_t i = idx % order.size();
    size_t j = (idx / 7) % order.size();
    std::swap(order[i], order[j]);
  }

  for (size_t idx : order) {
    EXPECT_EQ(dp_free(alloc, ptrs[idx]), 0);
  }

  EXPECT_EQ(alloc->free_list_head->next, nullptr)
      << "Free list not fully coalesced";
}
FUZZ_TEST(AllocatorFuzzTest, CoalescingWorks)
    .WithDomains(fuzztest::VectorOf(fuzztest::Arbitrary<size_t>()).WithMaxSize(30),
                 fuzztest::VectorOf(fuzztest::Arbitrary<size_t>()).WithMaxSize(50));

void DoubleFreeFails(size_t size) {
  size = (size % 128) + 1;

  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  void *ptr = dp_malloc(alloc, size);
  if (ptr == nullptr)
    return;

  memset(ptr, 0xDD, size);
  EXPECT_EQ(dp_free(alloc, ptr), 0);
  EXPECT_NE(dp_free(alloc, ptr), 0) << "Double free should fail";
}
FUZZ_TEST(AllocatorFuzzTest, DoubleFreeFails)
    .WithDomains(fuzztest::Arbitrary<size_t>());

void NullFreeDoesNotCrash() {
  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();
  dp_free(alloc, nullptr);
}
TEST(AllocatorFuzzTest, NullFreeDoesNotCrash) { NullFreeDoesNotCrash(); }

void ZeroSizeAllocation() {
  AllocatorFixture fixture;
  dp_alloc *alloc = fixture.get();

  void *ptr = dp_malloc(alloc, 0);
  if (ptr != nullptr) {
    EXPECT_EQ(dp_free(alloc, ptr), 0);
  }
}
TEST(AllocatorFuzzTest, ZeroSizeAllocation) { ZeroSizeAllocation(); }

} // namespace
