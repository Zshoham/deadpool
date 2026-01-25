#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <config_macros.h>

#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct block_header {
  struct block_header *next;
  size_t size;
  bool is_free;
} block_header;

typedef struct dp_alloc {
  uint8_t *buffer;
  size_t buffer_size;
  size_t available;
  block_header *free_list_head;

  IF_DP_LOG(dp_logger logger;)
  IF_DP_STATS(size_t num_iterations;)
} dp_alloc;

bool dp_init(dp_alloc *allocator, void *buffer, size_t buffer_size IF_DP_LOG(, dp_logger logger));
void *dp_malloc(dp_alloc *allocator, size_t size);
int dp_free(dp_alloc *allocator, void *ptr);
IF_DP_STATS(float dp_get_fragmentation(dp_alloc *allocator);)

#ifdef __cplusplus
}
#endif

#endif // ALLOCATOR_H
