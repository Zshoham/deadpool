#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "allocator_policies.h"

#define ALLOCATOR_BENCHMARK_INSTANTIATE(fixture, test, ...)                                        \
  BENCHMARK_TEMPLATE_INSTANTIATE_F(fixture, test, DeadpoolPolicy) __VA_ARGS__;                     \
  BENCHMARK_TEMPLATE_INSTANTIATE_F(fixture, test, MallocPolicy) __VA_ARGS__;                       \
  BENCHMARK_TEMPLATE_INSTANTIATE_F(fixture, test, MimallocPolicy) __VA_ARGS__;

constexpr size_t BUFFER_SIZE = 1024 * 1024;

template <typename Policy> class AllocatorFixture : public benchmark::Fixture {
public:
  void SetUp(benchmark::State &) override { m_policy.init(BUFFER_SIZE); }

  void TearDown(benchmark::State &) override { m_policy.teardown(); }

  void *alloc(size_t size) { return m_policy.alloc(size); }

  void free(void *ptr) { m_policy.free(ptr); }

protected:
  Policy m_policy;
};

// Single allocation benchmark
BENCHMARK_TEMPLATE_METHOD_F(AllocatorFixture, SingleAlloc)(benchmark::State &state) {
  size_t size = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    void *ptr = this->alloc(size);
    benchmark::DoNotOptimize(ptr);
    this->free(ptr);
  }
}
ALLOCATOR_BENCHMARK_INSTANTIATE(AllocatorFixture,
                                SingleAlloc, ->RangeMultiplier(4)->Range(16, 4096));

// Batch allocation benchmark - allocate N objects then free all
BENCHMARK_TEMPLATE_METHOD_F(AllocatorFixture, BatchAllocFree)(benchmark::State &state) {
  int count = state.range(0);
  size_t size = 64;
  std::vector<void *> ptrs(count);

  for (auto _ : state) {
    for (int i = 0; i < count; i++) {
      ptrs[i] = this->alloc(size);
    }
    for (int i = 0; i < count; i++) {
      this->free(ptrs[i]);
    }
  }
  state.SetItemsProcessed(state.iterations() * count * 2);
}
ALLOCATOR_BENCHMARK_INSTANTIATE(AllocatorFixture,
                                BatchAllocFree, ->RangeMultiplier(4)->Range(16, 256));

// Mixed workload - allocate/free in random order
BENCHMARK_TEMPLATE_METHOD_F(AllocatorFixture, MixedWorkload)(benchmark::State &state) {
  std::mt19937 rng(42);
  std::uniform_int_distribution<size_t> size_dist(16, 256);
  std::vector<void *> live_ptrs;
  live_ptrs.reserve(100);

  for (auto _ : state) {
    if (live_ptrs.size() < 50 || (live_ptrs.size() < 100 && rng() % 2 == 0)) {
      void *p = this->alloc(size_dist(rng));
      if (p)
        live_ptrs.push_back(p);
    } else if (!live_ptrs.empty()) {
      std::uniform_int_distribution<size_t> idx_dist(0, live_ptrs.size() - 1);
      size_t idx = idx_dist(rng);
      this->free(live_ptrs[idx]);
      live_ptrs[idx] = live_ptrs.back();
      live_ptrs.pop_back();
    }
  }

  for (void *p : live_ptrs) {
    this->free(p);
  }
}
ALLOCATOR_BENCHMARK_INSTANTIATE(AllocatorFixture, MixedWorkload);

// LIFO pattern - stack-like allocation
BENCHMARK_TEMPLATE_METHOD_F(AllocatorFixture, LifoPattern)(benchmark::State &state) {
  int depth = state.range(0);
  std::vector<void *> stack(depth);

  for (auto _ : state) {
    for (int i = 0; i < depth; i++) {
      stack[i] = this->alloc(64);
    }
    for (int i = depth - 1; i >= 0; i--) {
      this->free(stack[i]);
    }
  }
  state.SetItemsProcessed(state.iterations() * depth * 2);
}
ALLOCATOR_BENCHMARK_INSTANTIATE(AllocatorFixture,
                                LifoPattern, ->RangeMultiplier(2)->Range(512, 4096));

// FIFO pattern - queue-like allocation (first allocated, first freed)
BENCHMARK_TEMPLATE_METHOD_F(AllocatorFixture, FifoPattern)(benchmark::State &state) {
  int depth = state.range(0);
  std::vector<void *> queue(depth);

  for (auto _ : state) {
    for (int i = 0; i < depth; i++) {
      queue[i] = this->alloc(64);
    }
    for (int i = 0; i < depth; i++) {
      this->free(queue[i]);
    }
  }
  state.SetItemsProcessed(state.iterations() * depth * 2);
}
ALLOCATOR_BENCHMARK_INSTANTIATE(AllocatorFixture,
                                FifoPattern, ->RangeMultiplier(2)->Range(512, 4096));

// Fragmentation stress - create holes by freeing every other block, then allocate
// varying sizes to stress coalescing and best-fit behavior
BENCHMARK_TEMPLATE_METHOD_F(AllocatorFixture, FragmentationStress)(benchmark::State &state) {
  size_t small_size = 32;
  size_t large_size = static_cast<size_t>(state.range(0));
  int num_blocks = 128;
  std::vector<void *> ptrs(num_blocks);

  for (auto _ : state) {
    state.PauseTiming();
    // Allocate many small blocks
    for (int i = 0; i < num_blocks; i++) {
      ptrs[i] = this->alloc(small_size);
    }

    // Free every other block to create fragmentation (swiss cheese pattern)
    for (int i = 0; i < num_blocks; i += 2) {
      this->free(ptrs[i]);
      ptrs[i] = nullptr;
    }
    state.ResumeTiming();

    // Try to allocate larger blocks that require finding/coalescing holes
    std::vector<void *> large_ptrs;
    for (int i = 0; i < num_blocks / 4; i++) {
      void *p = this->alloc(large_size);
      if (p)
        large_ptrs.push_back(p);
    }
    state.PauseTiming();

    // Cleanup
    for (void *p : large_ptrs) {
      this->free(p);
    }
    for (int i = 1; i < num_blocks; i += 2) {
      this->free(ptrs[i]);
    }
  }
}
ALLOCATOR_BENCHMARK_INSTANTIATE(AllocatorFixture,
                                FragmentationStress, ->RangeMultiplier(2)->Range(512, 4096));

BENCHMARK_MAIN();
