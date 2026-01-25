#include <limits.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "allocator.h"

#define ILLEGAL_BLOCK_PTR UINTPTR_MAX

static const uint8_t default_align = alignof(max_align_t);

static inline uintptr_t align_address(uintptr_t address, size_t alignment) {
  return (address + (alignment - 1)) & ~(alignment - 1);
}

static block_header *next_phys(dp_alloc *allocator, block_header *block) {
  return (block_header *)((uint8_t *)block + block->size + sizeof(block_header));
}

bool dp_init(dp_alloc *allocator, void *buffer, size_t buffer_size IF_DP_LOG(, dp_logger logger)) {
  if (buffer == NULL || buffer_size < sizeof(block_header)) {
    return false;
  }

  uintptr_t aligned_start = align_address((uintptr_t)buffer, default_align);
  size_t alignment_offset = aligned_start - (uintptr_t)buffer;
  if (buffer_size <= alignment_offset + sizeof(block_header)) {
    return false;
  }

  allocator->buffer = (uint8_t *)aligned_start;
  allocator->buffer_size = buffer_size - alignment_offset;
  allocator->available = allocator->buffer_size - sizeof(block_header);
  IF_DP_LOG(allocator->logger = logger;)

  block_header *header = (block_header *)allocator->buffer;
  header->size = allocator->buffer_size - sizeof(block_header);
  header->is_free = true;
  header->next = NULL;

  allocator->free_list_head = header;
  return true;
}

void *dp_malloc(dp_alloc *allocator, size_t size) {
  /*
  Layout of allocated buffer:

            alignment
         ┌──────^───────┐
   size_t           1B
  |~~~~~~|       |~~~~~~|
  ┌──────┬───────┬──────┬───────────────────┐
  │header│padding│offset│    user buffer    │
  └──────┴───────┴──────┴───────────────────┘
                        ▲
                     user ptr

  header contains only the size of the buffer, including padding and offset.
  offset is used by free to get from the user pointer back to the header.
  padding is needed to fill the space for alignment.
   */

  size_t max_padding = default_align - 1 + 1; // alignment padding + 1 byte for offset
  size_t min_alloc_size = size + max_padding;

  if (size == 0 || allocator == NULL || min_alloc_size > allocator->available ||
      allocator->free_list_head == NULL) {
    return NULL;
  }

  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;
  block_header *prev_best_fit = NULL;
  block_header *best_fit = NULL;
  size_t best_fit_alloc_size = 0;
  size_t min_fit = UINTPTR_MAX;
  IF_DP_STATS(allocator->num_iterations = 1;)

  do {
    uintptr_t block_start = (uintptr_t)current + sizeof(block_header);
    uintptr_t aligned_user_ptr = align_address(block_start + 1, default_align);
    size_t padding = aligned_user_ptr - block_start;
    size_t alloc_size = size + padding;

    if (alloc_size <= current->size) {
      size_t fit = current->size - alloc_size;
      if (fit < min_fit) {
        best_fit = current;
        prev_best_fit = prev;
        best_fit_alloc_size = alloc_size;
        min_fit = fit;
      }
      if (min_fit == 0)
        break; // perfect fit.
    }
    prev = current;
    current = current->next;
    IF_DP_STATS(allocator->num_iterations++;)
  } while (current != NULL);

  if (best_fit == NULL)
    return NULL;

  uintptr_t next_block_addr = align_address(
      (uintptr_t)best_fit + sizeof(block_header) + best_fit_alloc_size, default_align);
  size_t actual_alloc_size = next_block_addr - (uintptr_t)best_fit - sizeof(block_header);
  size_t remainder = best_fit->size - actual_alloc_size;

  // Handle leftover space: if remainder is too small for a new block header,
  // remove best_fit from free list entirely. Otherwise, create a new free block.
  if (remainder < sizeof(block_header)) {
    actual_alloc_size = best_fit->size;
    if (best_fit == allocator->free_list_head) {
      allocator->free_list_head = best_fit->next;
    } else {
      prev_best_fit->next = best_fit->next;
    }
  } else {
    block_header *new_best_fit = (block_header *)next_block_addr;
    new_best_fit->size = best_fit->size - actual_alloc_size - sizeof(block_header);
    new_best_fit->is_free = true;
    new_best_fit->next = best_fit->next;

    // Link the new free block into the free list
    if (best_fit == allocator->free_list_head) {
      allocator->free_list_head = new_best_fit;
    } else {
      prev_best_fit->next = new_best_fit;
    }
    allocator->available -= sizeof(block_header); // Account for new header
  }

  best_fit->size = actual_alloc_size;
  best_fit->is_free = false;
  best_fit->next = NULL;
  allocator->available -= actual_alloc_size;

  uintptr_t block_start = (uintptr_t)best_fit + sizeof(block_header);
  uintptr_t aligned_user_ptr = align_address(block_start + 1, default_align);
  uint8_t offset = (uint8_t)(aligned_user_ptr - block_start);

  *((uint8_t *)aligned_user_ptr - 1) = offset;

  DP_INFO(allocator,
          "Allocated block at %p (size=%zu, offset=%u, free_list_head=%p, available=%zu)", best_fit,
          best_fit->size, offset, (void *)allocator->free_list_head, allocator->available);

  return (void *)aligned_user_ptr;
}

static block_header *coalsce(dp_alloc *allocator, block_header *free_block) {
  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;
  block_header *to_coalsce_left = NULL;
  block_header *to_coalsce_right = NULL;

  while (current != NULL) {
    if (next_phys(allocator, free_block) == current) {
      DP_DEBUG(allocator, "Found coalscing block on the right (free)%p-%p with (coalscing)%p-%p",
               free_block, current, current, next_phys(allocator, current));
      to_coalsce_right = current;
      if (current == allocator->free_list_head) {
        allocator->free_list_head = to_coalsce_right->next;
      } else {
        prev->next = to_coalsce_right->next;
      }
      current = to_coalsce_right->next;
      // to_coalsce_right->next = (block_header*)UINTPTR_MAX;
      to_coalsce_right->next = NULL;
      if (to_coalsce_left)
        break;
      else
        continue;
    }
    if (next_phys(allocator, current) == free_block) {
      DP_DEBUG(allocator, "Found coalscing block on the left (coalscing)%p-%p with (free)%p-%p",
               current, free_block, free_block, next_phys(allocator, free_block));
      to_coalsce_left = current;
      if (current == allocator->free_list_head) {
        allocator->free_list_head = to_coalsce_left->next;
      } else {
        prev->next = to_coalsce_left->next;
      }
      current = to_coalsce_left->next;
      // to_coalsce_left->next = (block_header*)UINTPTR_MAX;
      to_coalsce_left->next = NULL;
      if (to_coalsce_right)
        break;
      else
        continue;
    }

    prev = current;
    current = current->next;
  }

  if (to_coalsce_left == NULL && to_coalsce_right == NULL)
    return free_block;

  if (to_coalsce_left != NULL) {
    DP_DEBUG(allocator, "Coalscing left (cb=%zu, fb=%zu, avl=%zu)", to_coalsce_left->size,
             free_block->size, allocator->available);
    to_coalsce_left->size += sizeof(block_header) + free_block->size;
    allocator->available += sizeof(block_header);
    free_block = to_coalsce_left;
  }

  if (to_coalsce_right != NULL) {
    DP_DEBUG(allocator, "Coalscing left (fb=%zu, cb=%zu, avl=%zu)", free_block->size,
             to_coalsce_right->size, allocator->available);
    free_block->size += sizeof(block_header) + to_coalsce_right->size;
    allocator->available += sizeof(block_header);
  }

  DP_INFO(allocator, "Successfull coalscence (left=%p, right=%p, avl=%zu)", to_coalsce_left,
          to_coalsce_right, allocator->available);
  return free_block;
}

int dp_free(dp_alloc *allocator, void *ptr) {
  if (ptr == NULL || allocator == NULL) {
    DP_ERROR(allocator, "Trying to free null pointer, or with null allocator.");
    return 1;
  }

  uint8_t offset = *((uint8_t *)ptr - 1);
  block_header *to_free = (block_header *)((uint8_t *)ptr - offset - sizeof(block_header));

  // if (to_free->next != (block_header *)UINTPTR_MAX) {
  if (to_free->next != NULL) {
    DP_ERROR(allocator, "Trying to free %p which is not a valid block", to_free);
    return 1;
  }
  if ((uint8_t *)to_free < allocator->buffer ||
      (uint8_t *)to_free >= allocator->buffer + allocator->buffer_size) {
    DP_ERROR(allocator, "Deallocating invalid pointer %p", ptr);
    return 1; // Invalid pointer
  }
  if (to_free->is_free) {
    DP_ERROR(allocator, "Double free detected for pointer %p, block_size=%zu", ptr, to_free->size);
    return 1;
  }

  allocator->available += to_free->size;
  to_free->is_free = true;
  DP_INFO(allocator, "Freeing block at %p (ptr=%p, free_list_head=%p, available=%zu)", to_free, ptr,
          allocator->free_list_head, allocator->available);
  to_free = coalsce(allocator, to_free);
  to_free->next = allocator->free_list_head;
  allocator->free_list_head = to_free;

#if DP_FREE_VALIDATION
  block_header *current = allocator->free_list_head;
  uint32_t circle_lengh = 0;
  while (current != NULL) {
    if (current == (block_header *)UINTPTR_MAX) {
      DP_ERROR(allocator, "Free list is corrupted after freeing %p", to_free);
    }

    current = current->next;
    circle_lengh += 1;
    if (current == allocator->free_list_head) {
      DP_ERROR(allocator, "Free list is cirular, with length %u.", circle_lengh);
      return 1;
    }
  }

  DP_INFO(allocator, "Freed block at %p, free list has %u blocks", to_free, circle_lengh);
#endif

  return 0;
}

#if DP_STATS
float dp_get_fragmentation(dp_alloc *allocator) {
  size_t largest = 0;
  size_t total = 0;
  block_header *curr = allocator->free_list_head;
  while (curr) {
    total += curr->size;
    if (curr->size > largest)
      largest = curr->size;
    curr = curr->next;
  }

  return (total > 0) ? 1.0f - (float)largest / total : 0.0f;
}
#endif
