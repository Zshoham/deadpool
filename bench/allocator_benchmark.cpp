#include "allocator.h"
#include <benchmark/benchmark.h>
#include <cstring>
#include <random>
#include <vector>

// Utility class for benchmarks
class BenchmarkAllocator {
private:
  static const size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
  uint8_t *raw_buffer;
  dp_alloc allocator;

public:
  BenchmarkAllocator() {
    raw_buffer = new uint8_t[BUFFER_SIZE];
    dp_init(&allocator, raw_buffer, BUFFER_SIZE);
  }

  ~BenchmarkAllocator() { delete[] raw_buffer; }

  dp_alloc *get() { return &allocator; }
};

// Basic allocation and deallocation benchmark
static void BM_BasicAllocation(benchmark::State &state) {
  BenchmarkAllocator bench;
  const size_t allocation_size = state.range(0);

  for (auto _ : state) {
    void *ptr = dp_malloc(bench.get(), allocation_size);
    benchmark::DoNotOptimize(ptr);
    dp_free(bench.get(), ptr);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * allocation_size);
}

BENCHMARK(BM_BasicAllocation)
    ->RangeMultiplier(4)
    ->Range(8, 8192)
    ->Unit(benchmark::kMicrosecond);

// Multiple allocations of same size
static void BM_MultipleAllocations(benchmark::State &state) {
  BenchmarkAllocator bench;
  const int num_allocs = state.range(0);
  const size_t alloc_size = 64; // Fixed size
  std::vector<void *> ptrs;
  ptrs.reserve(num_allocs);

  for (auto _ : state) {
    for (int i = 0; i < num_allocs; i++) {
      void *ptr = dp_malloc(bench.get(), alloc_size);
      benchmark::DoNotOptimize(ptr);
      ptrs.push_back(ptr);
    }

    for (void *ptr : ptrs) {
      dp_free(bench.get(), ptr);
    }
    ptrs.clear();
  }

  state.SetItemsProcessed(state.iterations() * num_allocs);
  state.SetBytesProcessed(state.iterations() * num_allocs * alloc_size);
}

BENCHMARK(BM_MultipleAllocations)
    ->RangeMultiplier(2)
    ->Range(8, 128)
    ->Unit(benchmark::kMicrosecond);

// Fragmentation stress test
static void BM_FragmentationStress(benchmark::State &state) {
  BenchmarkAllocator bench;
  std::vector<void *> ptrs;
  const int num_allocs = state.range(0);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> size_dist(8, 256); // Random sizes

  for (auto _ : state) {
    // Phase 1: Allocate with random sizes
    for (int i = 0; i < num_allocs; i++) {
      size_t size = size_dist(gen);
      void *ptr = dp_malloc(bench.get(), size);
      benchmark::DoNotOptimize(ptr);
      if (ptr)
        ptrs.push_back(ptr);
    }

    // Phase 2: Free every other block to create fragmentation
    for (size_t i = 0; i < ptrs.size(); i += 2) {
      dp_free(bench.get(), ptrs[i]);
    }

    // Phase 3: Try to allocate larger blocks in the gaps
    for (size_t i = 0; i < ptrs.size(); i += 2) {
      size_t size = size_dist(gen) * 2; // Larger allocations
      void *ptr = dp_malloc(bench.get(), size);
      benchmark::DoNotOptimize(ptr);
    }

    // Cleanup
    for (size_t i = 1; i < ptrs.size(); i += 2) {
      dp_free(bench.get(), ptrs[i]);
    }
    ptrs.clear();
  }

  state.SetItemsProcessed(state.iterations() * num_allocs * 2);
}

BENCHMARK(BM_FragmentationStress)
    ->RangeMultiplier(2)
    ->Range(16, 256)
    ->Unit(benchmark::kMicrosecond);

// Extreme stress test - rapid allocations and deallocations with varying sizes
static void BM_ExtremeStress(benchmark::State &state) {
  BenchmarkAllocator bench;
  const int operations = state.range(0);
  std::vector<std::pair<void *, size_t>> allocations;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> size_dist(1, 1024); // 1B to 1KB
  std::uniform_real_distribution<> action_dist(0.0, 1.0);

  for (auto _ : state) {
    size_t total_allocated = 0;

    for (int i = 0; i < operations; i++) {
      double action = action_dist(gen);
      // 70% chance to allocate if under 900KB
      if (action < 0.7 && total_allocated < 900 * 1024) {
        size_t size = size_dist(gen);
        void *ptr = dp_malloc(bench.get(), size);
        if (ptr) {
          // Write to memory to ensure it's usable
          std::memset(ptr, 0xFF, size);
          benchmark::DoNotOptimize(ptr);
          allocations.push_back({ptr, size});
          total_allocated += size;
        }
      } else if (!allocations.empty()) {
        // Free random allocation
        size_t index = gen() % allocations.size();
        dp_free(bench.get(), allocations[index].first);
        total_allocated -= allocations[index].second;
        allocations.erase(allocations.begin() + index);
      }
    }

    // Cleanup remaining allocations
    for (auto &alloc : allocations) {
      dp_free(bench.get(), alloc.first);
    }
    allocations.clear();
  }

  state.SetItemsProcessed(state.iterations() * operations);
}

BENCHMARK(BM_ExtremeStress)
    ->RangeMultiplier(2)
    ->Range(1000, 10000)
    ->Unit(benchmark::kMillisecond);

// Real-world simulation - web server allocation pattern
static void BM_WebServerSimulation(benchmark::State &state) {
  BenchmarkAllocator bench;
  const int requests_per_iteration = state.range(0);
  std::vector<void *> request_buffers;
  std::vector<void *> response_buffers;

  // Typical sizes for web requests/responses
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> request_size(64, 1024);    // Small requests
  std::uniform_int_distribution<> response_size(1024, 8192); // Larger responses

  for (auto _ : state) {
    for (int i = 0; i < requests_per_iteration; i++) {
      // Allocate request buffer
      void *req_buf = dp_malloc(bench.get(), request_size(gen));
      benchmark::DoNotOptimize(req_buf);
      request_buffers.push_back(req_buf);

      // Allocate response buffer
      void *resp_buf = dp_malloc(bench.get(), response_size(gen));
      benchmark::DoNotOptimize(resp_buf);
      response_buffers.push_back(resp_buf);

      // Simulate processing by occasionally holding onto buffers
      if (i % 3 != 0) {
        dp_free(bench.get(), request_buffers.back());
        dp_free(bench.get(), response_buffers.back());
        request_buffers.pop_back();
        response_buffers.pop_back();
      }
    }

    // Cleanup remaining buffers
    for (auto ptr : request_buffers)
      dp_free(bench.get(), ptr);
    for (auto ptr : response_buffers)
      dp_free(bench.get(), ptr);
    request_buffers.clear();
    response_buffers.clear();
  }

  state.SetItemsProcessed(state.iterations() * requests_per_iteration * 2);
}

BENCHMARK(BM_WebServerSimulation)
    ->RangeMultiplier(2)
    ->Range(50, 400)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
