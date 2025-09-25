#include <cstdlib>
#include <ios>
#include <ostream>
#include <random>
#include <unordered_set>
#include <vector>
#include <cstring>
#include <iostream>

#include <benchmark/benchmark.h>

#include "allocator.h"

// Utility class for benchmarks
class BenchmarkAllocator {
public:
  static const size_t DEFAULT_BUFFER_SIZE = 1024 * 1024; // 1MB buffer
  uint8_t *raw_buffer;
  dp_alloc allocator;

  BenchmarkAllocator(size_t buffer_size=DEFAULT_BUFFER_SIZE) {
    raw_buffer = new uint8_t[buffer_size];
    dp_init(&allocator, raw_buffer, buffer_size);
  }

  ~BenchmarkAllocator() { delete[] raw_buffer; }
};

// Basic allocation and deallocation benchmark
static void BM_BasicAllocation(benchmark::State &state) {
  const size_t allocation_size = state.range(0);

  for (auto _ : state) {
    BenchmarkAllocator bench;
    void *ptr = dp_malloc(&bench.allocator, allocation_size);
    benchmark::DoNotOptimize(ptr);
    dp_free(&bench.allocator, ptr);
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
  const int num_allocs = state.range(0);
  const size_t alloc_size = state.range(1);
  std::vector<void *> ptrs;
  ptrs.reserve(num_allocs);

  for (auto _ : state) {
    BenchmarkAllocator bench(num_allocs * (alloc_size + 100) );
    for (int i = 0; i < num_allocs; i++) {
      void *ptr = dp_malloc(&bench.allocator, alloc_size);
      benchmark::DoNotOptimize(ptr);
      ptrs.push_back(ptr);
    }

    for (void *ptr : ptrs) {
      dp_free(&bench.allocator, ptr);
    }
    ptrs.clear();
  }

  state.SetItemsProcessed(state.iterations() * num_allocs);
  state.SetBytesProcessed(state.iterations() * num_allocs * alloc_size);
}

BENCHMARK(BM_MultipleAllocations)
    ->RangeMultiplier(2)
    ->Ranges({{8, 128}, {8, 4096}})
    ->Unit(benchmark::kMicrosecond);

// Fragmentation stress test
static void BM_FragmentationStress(benchmark::State &state) {
  std::vector<void *> ptrs;
  const int num_allocs = state.range(0);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> size_dist(8, 256); // Random sizes

  for (auto _ : state) {
    BenchmarkAllocator bench;
    // Phase 1: Allocate with random sizes
    for (int i = 0; i < num_allocs; i++) {
      size_t size = size_dist(gen);
      void *ptr = dp_malloc(&bench.allocator, size);
      benchmark::DoNotOptimize(ptr);
      if (ptr)
        ptrs.push_back(ptr);
    }

    // Phase 2: Free every other block to create fragmentation
    for (size_t i = 0; i < ptrs.size(); i += 2) {
      dp_free(&bench.allocator, ptrs[i]);
    }

    // Phase 3: Try to allocate larger blocks in the gaps
    for (size_t i = 0; i < ptrs.size(); i += 2) {
      size_t size = size_dist(gen) * 2; // Larger allocations
      void *ptr = dp_malloc(&bench.allocator, size);
      if (!ptr) {
        state.SkipWithError("Damn did not mind the gap.");
        exit(1);
      }
      benchmark::DoNotOptimize(ptr);
    }

    // Cleanup
    for (size_t i = 1; i < ptrs.size(); i += 2) {
      dp_free(&bench.allocator, ptrs[i]);
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
  const int operations = state.range(0);
  std::vector<std::pair<void *, size_t>> allocations;
  std::unordered_set<void*> alloc_set;
  std::mt19937 gen(23849);
  std::uniform_int_distribution<> size_dist(1, 1024); // 1B to 1KB
  std::uniform_real_distribution<> action_dist(0.0, 1.0);

  for (auto _ : state) {
    BenchmarkAllocator bench;
    size_t total_allocated = 0;

    for (int i = 0; i < operations; i++) {
      // std::cerr << "Bench: starting new loop" << std::endl;
      double action = action_dist(gen);
      // 70% chance to allocate if under 900KB
      // if ((total_allocated < 800*1024 || action < 0.7) && total_allocated < 980 * 1024) {
      if (action < 0.7 && total_allocated < 900 * 1024) {
        // std::cerr << "Bench: allocating..." << std::endl;
        size_t size = size_dist(gen);
        void *ptr = dp_malloc(&bench.allocator, size);
        if (ptr) {
          // Write to memory to ensure it's usable
          std::memset(ptr, 0xFF, size);
          benchmark::DoNotOptimize(ptr);
          allocations.push_back({ptr, size});
          if (alloc_set.contains(ptr)) {
            std::cerr << "Bench(Error): allocated the same pointer again... (" << i << ")" << std::endl;
            exit(1);
          }
          alloc_set.insert(ptr);
          total_allocated += size;
        } else {
          // std::cerr << "Bench(Error): alllocation failed" << std::endl;
        }
      } else if (!allocations.empty()) {
        // std::cerr << "Bench: freeing..." << std::endl;
        // Free random allocation
        size_t index = gen() % allocations.size();
        if (dp_free(&bench.allocator, allocations[index].first) != 0) {
          std::cerr << "Bench(Error): failed free" << std::endl;
          std::cerr << "Bench(Error): " << allocations[index].first << " in set ? " << std::boolalpha << alloc_set.contains(allocations[index].first) << std::endl;
        }
        total_allocated -= allocations[index].second;
        alloc_set.erase(allocations[index].first);
        allocations.erase(allocations.begin() + index);
      }
      // std::cerr << "Bench: finished loop validating pointers" << std::endl;
      for (auto ptr : allocations) {

        block_header *blk =
            (block_header *)((uint8_t *)ptr.first - sizeof(block_header));
        if (blk->size > 1024) {
          std::cerr << "Bench(Error): block " << blk << " has size=" << blk->size << std::endl;
        }
        if (blk->next != (block_header*)UINTPTR_MAX) {
          std::cerr << "Bench(Error) block " << blk << " is no longer valid..." << std::endl;
        }
        if (blk->is_free == true) {
          std::cerr << "Bench(Error) block " << blk << " is free?..." << std::endl;
        }
      }
    }

    // Cleanup remaining allocations
    // std::cerr << "Bench: freeing remaining pointers" << std::endl;
    for (auto &alloc : allocations) {
      dp_free(&bench.allocator, alloc.first);
    }
    allocations.clear();
    alloc_set.clear();
  }

  state.SetItemsProcessed(state.iterations() * operations);
}

BENCHMARK(BM_ExtremeStress)
    ->RangeMultiplier(2)
    ->Range(1000, 10000)
    ->Unit(benchmark::kMillisecond);

// Real-world simulation - web server allocation pattern
static void BM_WebServerSimulation(benchmark::State &state) {
  const int requests_per_iteration = state.range(0);
  std::vector<void *> request_buffers;
  std::vector<void *> response_buffers;
  std::vector<size_t> request_sizes;
  std::vector<size_t> response_sizes;

  // Typical sizes for web requests/responses
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> request_size_dist(64, 1024);    // Small requests
  std::uniform_int_distribution<> response_size_dist(1024, 8192); // Larger responses

  for (int i = 0; i < requests_per_iteration; i++) {
    request_sizes.push_back(request_size_dist(gen));
    response_sizes.push_back(response_size_dist(gen));
  }

  for (auto _ : state) {
    BenchmarkAllocator bench(((1024 + 8192) * requests_per_iteration) * 0.5);
    // dp_init(&bench.allocator, bench.raw_buffer, BenchmarkAllocator::BUFFER_SIZE);
    for (int i = 0; i < requests_per_iteration; i++) {
      // Allocate request buffer
      void *req_buf = dp_malloc(&bench.allocator, request_sizes[i]);
      if (!req_buf) {
        std::cout << "getting NULL req ??" << bench.allocator.available << std::endl;
      }
      std::memset(req_buf, 0xFF, request_sizes[i]);
      benchmark::DoNotOptimize(req_buf);
      request_buffers.push_back(req_buf);

      // Allocate response buffer
      void *resp_buf = dp_malloc(&bench.allocator, response_sizes[i]);
      if (!resp_buf) { 
        std::cout << "getting NULL resp ??" << bench.allocator.available << std::endl;
      }
      std::memset(resp_buf, 0xFF, response_sizes[i]);
      benchmark::DoNotOptimize(resp_buf);
      response_buffers.push_back(resp_buf);

      // Simulate processing by occasionally holding onto buffers
      if (i % 3 != 0) {
        dp_free(&bench.allocator, request_buffers.back());
        dp_free(&bench.allocator, response_buffers.back());
        request_buffers.pop_back();
        response_buffers.pop_back();
      }
    }

    // Cleanup remaining buffers
    for (auto ptr : request_buffers)
      dp_free(&bench.allocator, ptr);
    for (auto ptr : response_buffers)
      dp_free(&bench.allocator, ptr);
    request_buffers.clear();
    response_buffers.clear();
  }

  state.SetItemsProcessed(state.iterations() * requests_per_iteration * 2);
}

BENCHMARK(BM_WebServerSimulation)
    ->RangeMultiplier(8)
    ->Range(8, 1024)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
