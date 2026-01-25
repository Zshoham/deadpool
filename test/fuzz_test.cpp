#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

#include "test_common.hpp"

class FuzzTest : public DPAllocatorTest {
protected:
  std::mt19937 rng;

  void SetUp() override {
    DPAllocatorTest::SetUp();
    rng.seed(std::random_device{}());
  }

  void seed(uint32_t s) { rng.seed(s); }

  size_t random_size(size_t min, size_t max) {
    std::uniform_int_distribution<size_t> dist(min, max);
    return dist(rng);
  }

  int random_int(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
  }
};

TEST_F(FuzzTest, RandomAllocFreeSequence) {
  seed(0xDEADBEEF);
  constexpr int NUM_OPERATIONS = 10000;

  std::vector<AllocationMetadata> live;

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    bool should_alloc = live.empty() || random_int(0, 2) != 0;

    if (live.size() >= 20) {
      should_alloc = false;
    }

    if (should_alloc) {
      size_t size = random_size(1, 128);
      void *ptr = dp_malloc(&allocator, size);
      if (ptr) {
        memset(ptr, 0xAB, size);
        live.push_back({ptr, size});
      }
    } else if (!live.empty()) {
      size_t idx = random_size(0, live.size() - 1);
      ASSERT_EQ(dp_free(&allocator, live[idx].ptr), 0) << "Free failed at op " << i;
      live.erase(live.begin() + static_cast<long>(idx));
    }
  }

  for (auto &alloc : live) {
    ASSERT_EQ(dp_free(&allocator, alloc.ptr), 0);
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

TEST_F(FuzzTest, RandomSizeDistributions) {
  seed(0xCAFEBABE);
  constexpr int NUM_ITERATIONS = 1000;

  auto run_with_dist = [&](size_t min_size, size_t max_size) {
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
      size_t size = random_size(min_size, max_size);
      void *ptr = dp_malloc(&allocator, size);
      if (ptr) {
        memset(ptr, 0xCD, size);
        ASSERT_EQ(dp_free(&allocator, ptr), 0);
      }
    }
    EXPECT_EQ(allocator.free_list_head->next, nullptr);
  };

  run_with_dist(1, 8);
  run_with_dist(1, 32);
  run_with_dist(1, 64);
  run_with_dist(16, 128);
  run_with_dist(1, 256);
}

TEST_F(FuzzTest, RandomFreeOrder) {
  seed(0xFEEDFACE);
  constexpr size_t ALLOC_SIZE = 16;

  std::vector<void *> ptrs;
  for (;;) {
    void *p = dp_malloc(&allocator, ALLOC_SIZE);
    if (!p)
      break;
    memset(p, 0xEE, ALLOC_SIZE);
    ptrs.push_back(p);
  }

  ASSERT_GT(ptrs.size(), 5u);

  std::shuffle(ptrs.begin(), ptrs.end(), rng);

  for (void *p : ptrs) {
    ASSERT_EQ(dp_free(&allocator, p), 0);
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

TEST_F(FuzzTest, AllocateWriteVerifyFree) {
  seed(0x12345678);
  constexpr int NUM_OPERATIONS = 2000;

  struct TrackedAlloc {
    void *ptr;
    size_t size;
    uint8_t pattern;
  };
  std::vector<TrackedAlloc> live;

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    bool should_alloc = live.empty() || random_int(0, 2) != 0;

    if (live.size() >= 15) {
      should_alloc = false;
    }

    if (should_alloc) {
      size_t size = random_size(1, 100);
      uint8_t pattern = static_cast<uint8_t>(random_int(0, 255));
      void *ptr = dp_malloc(&allocator, size);
      if (ptr) {
        memset(ptr, pattern, size);
        live.push_back({ptr, size, pattern});
      }
    } else if (!live.empty()) {
      size_t idx = random_size(0, live.size() - 1);
      auto &alloc = live[idx];

      for (size_t j = 0; j < alloc.size; ++j) {
        uint8_t actual = static_cast<uint8_t *>(alloc.ptr)[j];
        ASSERT_EQ(actual, alloc.pattern)
            << "Memory corruption at byte " << j << " of allocation at op " << i;
      }

      ASSERT_EQ(dp_free(&allocator, alloc.ptr), 0);
      live.erase(live.begin() + static_cast<long>(idx));
    }
  }

  for (auto &alloc : live) {
    for (size_t j = 0; j < alloc.size; ++j) {
      uint8_t actual = static_cast<uint8_t *>(alloc.ptr)[j];
      ASSERT_EQ(actual, alloc.pattern);
    }
    ASSERT_EQ(dp_free(&allocator, alloc.ptr), 0);
  }
}

TEST_F(FuzzTest, BurstAllocBurstFree) {
  seed(0xABCDEF01);
  constexpr int NUM_ROUNDS = 100;

  for (int round = 0; round < NUM_ROUNDS; ++round) {
    int burst_size = random_int(1, 10);
    std::vector<void *> burst_ptrs;

    for (int i = 0; i < burst_size; ++i) {
      size_t size = random_size(1, 64);
      void *p = dp_malloc(&allocator, size);
      if (p) {
        memset(p, 0xFF, size);
        burst_ptrs.push_back(p);
      }
    }

    if (random_int(0, 1) == 0) {
      std::shuffle(burst_ptrs.begin(), burst_ptrs.end(), rng);
    }

    for (void *p : burst_ptrs) {
      ASSERT_EQ(dp_free(&allocator, p), 0);
    }
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

TEST_F(FuzzTest, PowerOfTwoSizes) {
  seed(0x87654321);
  constexpr int NUM_ITERATIONS = 500;

  std::vector<void *> live;
  size_t power_sizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};

  for (int i = 0; i < NUM_ITERATIONS; ++i) {
    bool should_alloc = live.empty() || random_int(0, 1) == 0;

    if (live.size() >= 10) {
      should_alloc = false;
    }

    if (should_alloc) {
      size_t idx = random_size(0, sizeof(power_sizes) / sizeof(power_sizes[0]) - 1);
      size_t size = power_sizes[idx];
      void *p = dp_malloc(&allocator, size);
      if (p) {
        memset(p, 0x55, size);
        live.push_back(p);
      }
    } else if (!live.empty()) {
      size_t idx = random_size(0, live.size() - 1);
      ASSERT_EQ(dp_free(&allocator, live[idx]), 0);
      live.erase(live.begin() + static_cast<long>(idx));
    }
  }

  for (void *p : live) {
    ASSERT_EQ(dp_free(&allocator, p), 0);
  }
}

TEST_F(FuzzTest, AlignmentStress) {
  seed(0x11223344);
  constexpr int NUM_ITERATIONS = 1000;

  for (int i = 0; i < NUM_ITERATIONS; ++i) {
    size_t size = random_size(1, 200);
    void *ptr = dp_malloc(&allocator, size);
    if (ptr) {
      uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
      ASSERT_EQ(addr % DEFAULT_ALIGN, 0u) << "Misaligned pointer at iteration " << i;

      memset(ptr, 0xAA, size);
      ASSERT_EQ(dp_free(&allocator, ptr), 0);
    }
  }
}

TEST_F(FuzzTest, EdgeSizeAllocations) {
  seed(0x99887766);

  size_t edge_sizes[] = {0,
                         1,
                         sizeof(block_header) - 1,
                         sizeof(block_header),
                         sizeof(block_header) + 1,
                         DEFAULT_ALIGN - 1,
                         DEFAULT_ALIGN,
                         DEFAULT_ALIGN + 1,
                         BUFFER_SIZE / 4,
                         BUFFER_SIZE / 2,
                         BUFFER_SIZE - sizeof(block_header) - DEFAULT_ALIGN,
                         BUFFER_SIZE - 1,
                         BUFFER_SIZE,
                         BUFFER_SIZE + 1};

  for (size_t size : edge_sizes) {
    void *p = dp_malloc(&allocator, size);
    if (p) {
      if (size > 0) {
        memset(p, 0xBB, size);
      }
      ASSERT_EQ(dp_free(&allocator, p), 0) << "Free failed for edge size " << size;
    }
  }
}

TEST_F(FuzzTest, RepeatedSameSizeAlloc) {
  seed(0xDEADC0DE);
  constexpr int NUM_ITERATIONS = 500;

  size_t fixed_size = random_size(8, 64);
  std::vector<void *> live;

  for (int i = 0; i < NUM_ITERATIONS; ++i) {
    bool should_alloc = live.empty() || random_int(0, 2) != 0;

    if (live.size() >= 12) {
      should_alloc = false;
    }

    if (should_alloc) {
      void *p = dp_malloc(&allocator, fixed_size);
      if (p) {
        memset(p, 0xCC, fixed_size);
        live.push_back(p);
      }
    } else if (!live.empty()) {
      size_t idx = random_size(0, live.size() - 1);
      ASSERT_EQ(dp_free(&allocator, live[idx]), 0);
      live.erase(live.begin() + static_cast<long>(idx));
    }
  }

  for (void *p : live) {
    ASSERT_EQ(dp_free(&allocator, p), 0);
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

TEST_F(FuzzTest, MultipleSeedsConsistency) {
  uint32_t seeds[] = {0, 1, 42, 0xFFFFFFFF, 0x12345678, 0xDEADBEEF};

  for (uint32_t s : seeds) {
    SetUp();
    seed(s);

    std::vector<void *> ptrs;
    for (int i = 0; i < 50; ++i) {
      size_t size = random_size(1, 64);
      void *p = dp_malloc(&allocator, size);
      if (p) {
        memset(p, 0xDD, size);
        ptrs.push_back(p);
      }

      if (!ptrs.empty() && random_int(0, 2) == 0) {
        size_t idx = random_size(0, ptrs.size() - 1);
        ASSERT_EQ(dp_free(&allocator, ptrs[idx]), 0);
        ptrs.erase(ptrs.begin() + static_cast<long>(idx));
      }
    }

    for (void *p : ptrs) {
      ASSERT_EQ(dp_free(&allocator, p), 0);
    }

    EXPECT_EQ(allocator.free_list_head->next, nullptr) << "Coalescing failed for seed " << s;
  }
}

TEST_F(FuzzTest, LIFOFreeing) {
  seed(0xBEEFCAFE);
  constexpr int NUM_ROUNDS = 200;

  for (int round = 0; round < NUM_ROUNDS; ++round) {
    std::vector<void *> stack;
    int depth = random_int(1, 8);

    for (int i = 0; i < depth; ++i) {
      size_t size = random_size(1, 32);
      void *p = dp_malloc(&allocator, size);
      if (p) {
        memset(p, 0x77, size);
        stack.push_back(p);
      }
    }

    while (!stack.empty()) {
      ASSERT_EQ(dp_free(&allocator, stack.back()), 0);
      stack.pop_back();
    }
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

TEST_F(FuzzTest, FIFOFreeing) {
  seed(0xCAFED00D);
  constexpr int NUM_ROUNDS = 200;

  for (int round = 0; round < NUM_ROUNDS; ++round) {
    std::vector<void *> queue;
    int depth = random_int(1, 8);

    for (int i = 0; i < depth; ++i) {
      size_t size = random_size(1, 32);
      void *p = dp_malloc(&allocator, size);
      if (p) {
        memset(p, 0x88, size);
        queue.push_back(p);
      }
    }

    for (void *p : queue) {
      ASSERT_EQ(dp_free(&allocator, p), 0);
    }
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

TEST_F(FuzzTest, InterleavedPatterns) {
  seed(0xFACEFEED);
  constexpr int NUM_OPS = 3000;

  struct TrackedPtr {
    void *ptr;
    size_t size;
  };
  std::vector<TrackedPtr> live;

  for (int i = 0; i < NUM_OPS; ++i) {
    int action = random_int(0, 4);

    switch (action) {
    case 0:
    case 1:
    case 2: {
      size_t size = random_size(1, 80);
      void *p = dp_malloc(&allocator, size);
      if (p) {
        memset(p, 0x99, size);
        live.push_back({p, size});
      }
      break;
    }
    case 3: {
      if (!live.empty()) {
        size_t idx = random_size(0, live.size() - 1);
        ASSERT_EQ(dp_free(&allocator, live[idx].ptr), 0);
        live.erase(live.begin() + static_cast<long>(idx));
      }
      break;
    }
    case 4: {
      if (live.size() >= 2) {
        for (int j = 0; j < 2 && !live.empty(); ++j) {
          size_t idx = random_size(0, live.size() - 1);
          ASSERT_EQ(dp_free(&allocator, live[idx].ptr), 0);
          live.erase(live.begin() + static_cast<long>(idx));
        }
      }
      break;
    }
    }

    if (live.size() > 25) {
      while (live.size() > 15) {
        size_t idx = random_size(0, live.size() - 1);
        ASSERT_EQ(dp_free(&allocator, live[idx].ptr), 0);
        live.erase(live.begin() + static_cast<long>(idx));
      }
    }
  }

  for (auto &alloc : live) {
    ASSERT_EQ(dp_free(&allocator, alloc.ptr), 0);
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}
