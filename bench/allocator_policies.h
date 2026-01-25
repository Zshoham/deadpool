#pragma once

#include <cstdlib>
#include <vector>

#include <mimalloc.h>
#include <o1heap.h>

extern "C" {
#include "allocator.h"
}

#if DP_LOG
static void noop_log(const char *, ...) {}
static dp_logger null_logger = {noop_log, noop_log, noop_log, noop_log};
#endif

struct DeadpoolPolicy {
  std::vector<uint8_t> buffer;
  dp_alloc allocator{};

  void init(size_t size) {
    buffer.resize(size);
    dp_init(&allocator, buffer.data(), size IF_DP_LOG(, null_logger));
  }

  void *alloc(size_t size) { return dp_malloc(&allocator, size); }

  void free(void *ptr) { dp_free(&allocator, ptr); }

  void teardown() { buffer.clear(); }
};

struct MallocPolicy {
  void init(size_t) {}

  void *alloc(size_t size) { return std::malloc(size); }

  void free(void *ptr) { std::free(ptr); }

  void teardown() {}
};

struct MimallocPolicy {
  void init(size_t) {}

  void *alloc(size_t size) { return mi_malloc(size); }

  void free(void *ptr) { mi_free(ptr); }

  void teardown() {}
};

struct O1HeapPolicy {
  std::vector<uint8_t> buffer;
  O1HeapInstance *heap{};

  void init(size_t size) {
    buffer.resize(size);
    heap = o1heapInit(buffer.data(), size);
  }

  void *alloc(size_t size) { return o1heapAllocate(heap, size); }

  void free(void *ptr) { o1heapFree(heap, ptr); }

  void teardown() { buffer.clear(); }
};
