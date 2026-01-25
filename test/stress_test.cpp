#include <random>

#include "test_common.hpp"

// High-iteration alternating alloc/free - exercises coalescing under repeated use
TEST_F(DPAllocatorTest, HighIterationAlternatingSmallAlloc) {
  constexpr int NUM_ITERATIONS = 10000;
  constexpr size_t ALLOC_SIZE = 16;

  for (int i = 0; i < NUM_ITERATIONS; ++i) {
    void *ptr = dp_malloc(&allocator, ALLOC_SIZE);
    ASSERT_NE(ptr, nullptr) << "Allocation failed at iteration " << i;

    ASSERT_EQ(dp_free(&allocator, ptr), 0) << "Free failed at iteration " << i;

#if DP_STATS
    if ((i % 1000) == 0) {
      float frag = dp_get_fragmentation(&allocator);
      EXPECT_LE(frag, 0.01f) << "Unexpected fragmentation after iteration " << i;
    }
#endif
  }

#if DP_STATS
  float final_frag = dp_get_fragmentation(&allocator);
  EXPECT_LE(final_frag, 0.01f);
#endif
}

// Varied allocation sizes in alternating pattern
TEST_F(DPAllocatorTest, AlternatingAllocationFreeingVariedSizes) {
  constexpr int NUM_ITERATIONS = 1000;
  std::mt19937 rng(42u);
  std::uniform_int_distribution<size_t> size_dist(1, 128);

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    size_t size = size_dist(rng);
    void *ptr = dp_malloc(&allocator, size);
    ASSERT_NE(ptr, nullptr) << "Allocation failed at iteration " << i << " size " << size;
    ASSERT_EQ(dp_free(&allocator, ptr), 0) << "Free failed at iteration " << i;
  }
}

// Deterministic randomized alloc/free stress with varied sizes
TEST_F(DPAllocatorTest, DeterministicRandomAllocFreeStress) {
  constexpr int NUM_STEPS = 5000;
  std::mt19937 rng(123456u);
  std::uniform_int_distribution<int> op_dist(0, 2);
  std::uniform_int_distribution<size_t> size_dist(1, 64);

  std::vector<void *> live_ptrs;

  for (int step = 0; step < NUM_STEPS; ++step) {
    int op = op_dist(rng);
    bool do_alloc = live_ptrs.empty() || op != 0;

    if (live_ptrs.size() > 15) {
      do_alloc = false;
    }

    if (do_alloc) {
      size_t size = size_dist(rng);
      void *ptr = dp_malloc(&allocator, size);
      if (ptr) {
        live_ptrs.push_back(ptr);
      }
    } else if (!live_ptrs.empty()) {
      std::uniform_int_distribution<size_t> idx_dist(0, live_ptrs.size() - 1);
      size_t idx = idx_dist(rng);
      ASSERT_EQ(dp_free(&allocator, live_ptrs[idx]), 0) << "Random free failed at step " << step;
      live_ptrs.erase(live_ptrs.begin() + static_cast<long>(idx));
    }
  }

  for (void *p : live_ptrs) {
    ASSERT_EQ(dp_free(&allocator, p), 0);
  }

  ASSERT_NE(allocator.free_list_head, nullptr);
  EXPECT_EQ(allocator.free_list_head->next, nullptr) << "All blocks should coalesce after cleanup";
}

// Explicit fragmentation and coalescing under near-full memory pressure
TEST_F(DPAllocatorTest, FragmentationAndCoalescingUnderPressure) {
  std::vector<void *> ptrs;

  for (;;) {
    void *p = dp_malloc(&allocator, 8);
    if (p == nullptr) {
      break;
    }
    ptrs.push_back(p);
  }

  ASSERT_GT(ptrs.size(), 0u) << "Failed to allocate any blocks in stress test";

  for (size_t i = 0; i < ptrs.size(); i += 2) {
    ASSERT_EQ(dp_free(&allocator, ptrs[i]), 0) << "Free (even index) failed";
  }

#if DP_STATS
  float frag_fragmented = dp_get_fragmentation(&allocator);
  EXPECT_GT(frag_fragmented, 0.1f) << "Fragmentation did not increase after partial frees";
#endif

  for (size_t i = 1; i < ptrs.size(); i += 2) {
    ASSERT_EQ(dp_free(&allocator, ptrs[i]), 0) << "Free (odd index) failed";
  }

  ASSERT_NE(allocator.free_list_head, nullptr);
  EXPECT_TRUE(allocator.free_list_head->is_free);
  EXPECT_EQ(allocator.free_list_head->next, nullptr) << "Expected a single coalesced free block";

#if DP_STATS
  float frag_final = dp_get_fragmentation(&allocator);
  EXPECT_LE(frag_final, 0.05f) << "Fragmentation should be very low after full coalescing";
#endif
}

// Large and small interleaved allocations under pressure
TEST_F(DPAllocatorTest, LargeAndSmallInterleavedUnderPressure) {
  size_t large_size = BUFFER_SIZE / 3;
  void *large = dp_malloc(&allocator, large_size);
  ASSERT_NE(large, nullptr);

  std::vector<void *> smalls;
  for (;;) {
    void *p = dp_malloc(&allocator, 16);
    if (!p)
      break;
    smalls.push_back(p);
  }
  ASSERT_GT(smalls.size(), 0u);

  for (size_t i = 0; i < smalls.size(); i += 2) {
    ASSERT_EQ(dp_free(&allocator, smalls[i]), 0);
  }

  ASSERT_EQ(dp_free(&allocator, large), 0);

  void *same_large = dp_malloc(&allocator, large_size);
  ASSERT_NE(same_large, nullptr) << "Allocator failed to reuse freed large block";

  ASSERT_EQ(dp_free(&allocator, same_large), 0);
  for (size_t i = 1; i < smalls.size(); i += 2) {
    ASSERT_EQ(dp_free(&allocator, smalls[i]), 0);
  }

  ASSERT_NE(allocator.free_list_head, nullptr);
  EXPECT_EQ(allocator.free_list_head->next, nullptr)
      << "All blocks should coalesce after full cleanup";

#if DP_STATS
  float frag = dp_get_fragmentation(&allocator);
  EXPECT_LE(frag, 0.05f);
#endif
}

// Best-fit traversal complexity under fragmentation
TEST_F(DPAllocatorTest, Complexity) {
  const int N = 20;
  std::vector<void *> ptrs;
  for (int i = 0; i < N; ++i) {
    void *p;
    checked_alloc(10, &p);
    ptrs.push_back(p);
  }

  for (int i = 0; i < N; i += 2) {
    checked_free(ptrs[i]);
  }

  size_t list_len = 0;
  block_header *curr = allocator.free_list_head;
  while (curr) {
    list_len++;
    curr = curr->next;
  }
  test_info("List length: %zu\n", list_len);

  void *p;
  p = dp_malloc(&allocator, 9);
  ASSERT_NE(p, nullptr);
  allocated.push_back({p, 9});

#if DP_STATS
  test_info("Complexity check: N=%d, iterations=%zu\n", N / 2, allocator.num_iterations);
  ASSERT_GE(allocator.num_iterations, N / 2);
#endif
}

// Rapid fill and drain cycles
TEST_F(DPAllocatorTest, RapidFillDrainCycles) {
  constexpr int NUM_CYCLES = 50;

  for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
    std::vector<void *> ptrs;

    for (;;) {
      void *p = dp_malloc(&allocator, 16);
      if (!p)
        break;
      ptrs.push_back(p);
    }

    ASSERT_GT(ptrs.size(), 0u) << "No allocations in cycle " << cycle;

    for (void *p : ptrs) {
      ASSERT_EQ(dp_free(&allocator, p), 0) << "Free failed in cycle " << cycle;
    }

    ASSERT_NE(allocator.free_list_head, nullptr);
    EXPECT_EQ(allocator.free_list_head->next, nullptr)
        << "Free list should have single block after drain in cycle " << cycle;
  }
}

// Reverse-order free pattern
TEST_F(DPAllocatorTest, ReverseOrderFreePattern) {
  std::vector<void *> ptrs;
  constexpr size_t ALLOC_SIZE = 32;

  for (;;) {
    void *p = dp_malloc(&allocator, ALLOC_SIZE);
    if (!p)
      break;
    ptrs.push_back(p);
  }

  ASSERT_GT(ptrs.size(), 3u);

  for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
    ASSERT_EQ(dp_free(&allocator, *it), 0);
  }

  ASSERT_NE(allocator.free_list_head, nullptr);
  EXPECT_EQ(allocator.free_list_head->next, nullptr) << "All blocks should coalesce into one";

#if DP_STATS
  float frag = dp_get_fragmentation(&allocator);
  EXPECT_LE(frag, 0.01f);
#endif
}

// Mixed size stress with deterministic pattern
TEST_F(DPAllocatorTest, MixedSizeStress) {
  constexpr int NUM_ITERATIONS = 500;
  std::mt19937 rng(987654u);
  std::uniform_int_distribution<size_t> size_dist(1, 200);

  std::vector<void *> live_ptrs;

  for (int i = 0; i < NUM_ITERATIONS; ++i) {
    size_t size = size_dist(rng);
    void *p = dp_malloc(&allocator, size);
    if (p) {
      live_ptrs.push_back(p);
    }

    if (live_ptrs.size() > 5 && (i % 3) == 0) {
      std::uniform_int_distribution<size_t> idx_dist(0, live_ptrs.size() - 1);
      size_t idx = idx_dist(rng);
      ASSERT_EQ(dp_free(&allocator, live_ptrs[idx]), 0);
      live_ptrs.erase(live_ptrs.begin() + static_cast<long>(idx));
    }
  }

  for (void *p : live_ptrs) {
    ASSERT_EQ(dp_free(&allocator, p), 0);
  }

  ASSERT_NE(allocator.free_list_head, nullptr);
  EXPECT_EQ(allocator.free_list_head->next, nullptr);
}

// Boundary allocation sizes
TEST_F(DPAllocatorTest, BoundaryAllocationSizes) {
  size_t sizes[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65};

  for (size_t size : sizes) {
    void *p = dp_malloc(&allocator, size);
    if (p) {
      ASSERT_EQ(dp_free(&allocator, p), 0) << "Failed for size " << size;
    }
  }

  EXPECT_EQ(allocator.free_list_head->next, nullptr)
      << "Should be single free block after all boundary tests";
}
