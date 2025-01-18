#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "allocator.h"

// Helper function to align an address
// static uintptr_t align_address(uintptr_t address, size_t alignment) {
//   if (alignment <= 1)
//     return address; // No alignment needed
//   return (address + (alignment - 1)) & ~(alignment - 1);
// }

static block_header *next_phys(dp_alloc *allocator, block_header *block) {
  return (block_header *)((uint8_t *)block + block->size +
                          sizeof(block_header));
}

bool dp_init(dp_alloc *allocator, void *buffer, size_t buffer_size) {
  if (buffer == NULL || buffer_size < sizeof(block_header)) {
    return false;
  }
  allocator->buffer = (uint8_t *)buffer;
  allocator->buffer_size = buffer_size;
  allocator->available = buffer_size - sizeof(block_header);

  block_header *header = (block_header *)allocator->buffer;
  header->size = buffer_size - sizeof(block_header);
  header->is_free = true;
  header->next = NULL;

  allocator->free_list_head = header;
  return true;
}

void *dp_malloc(dp_alloc *allocator, size_t size) {
  size_t required_size = size + sizeof(block_header);

  if (size == 0 || allocator == NULL || required_size > allocator->available ||
      allocator->free_list_head == NULL) {
    return NULL;
  }

  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;
  block_header *prev_best_fit = NULL;
  block_header *best_fit = current;
  size_t min_fit = UINTPTR_MAX;

  do {
    size_t fit = current->size - size;
    if (fit == 0) {
      min_fit = 0;
      break;
    }
    if (fit < min_fit) {
      best_fit = current;
      prev_best_fit = prev;
      min_fit = fit;
    }
    prev = current;
    current = current->next;
  } while (current != NULL);

  if (min_fit > best_fit->size)
    return NULL;

  if (prev_best_fit) {
    prev_best_fit->next = best_fit->next;
  }

  if (min_fit > sizeof(size_t) * 2) {
    block_header *new_free_head =
        (block_header *)((uint8_t *)best_fit + required_size);
    new_free_head->next = allocator->free_list_head->next;
    new_free_head->size = best_fit->size - required_size;
    new_free_head->is_free = true;
    allocator->free_list_head = new_free_head;
  }

  best_fit->size = size;
  best_fit->is_free = false;
  allocator->available -= best_fit->size + sizeof(block_header);
  return (uint8_t *)best_fit + sizeof(block_header);
}

// void join(block_header *left, block_header *right) {
//   left->size += right->size + sizeof(block_header);
//   left->next = right->next;
// }

// static void coalsce(dp_alloc *allocator, block_header *free_block) {
//   // free_list_head is free_block so we always want to start with
//   // the next one.
//   block_header *current = allocator->free_list_head->next;
//   block_header *prev = NULL;
//   // This is only relevant if both adjacent physical blocks are
//   // free. and then it keeps track of the prev block of free_block
//   // after the first join in order to update it if needed.
//   block_header *free_block_prev = NULL;
//
//   while (current != NULL) {
//     if (next_phys(free_block) == current) {
//       join(free_block, current);
//       allocator->available += sizeof(block_header);
//       if (free_block_prev) {
//         prev->next = current->next;
//         // This is the second block we join with to_free,
//         // it means that we had a free block on both sides,
//         // of the original to_free and we merged them all.
//         // so there is nothing more we can do.
//         break;
//       }
//       free_block_prev = prev;
//     }
//
//     if (next_phys(current) == free_block) {
//       join(current, free_block);
//       allocator->available += sizeof(block_header);
//       free_block = current;
//       if (free_block_prev) {
//         prev->next = current->next;
//         // to_free is already part of the list.
//         // because it was coalesced with another
//         // block before.
//         free_block_prev->next = free_block;
//         // like before this this means that we merged the two
//         // adjacent blocks to the original to_free into it,
//         // so there is nothing more to do.
//         break;
//       }
//       free_block_prev = prev;
//     }
//     // if (prev)
//     //   prev->next = free_block;
//     // else
//     //   allocator->free_list_head = free_block;
//
//     prev = current;
//     current = current->next;
//   }
// }


static bool try_coalsce(dp_alloc *allocator, block_header *free_block) {
  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;

  int coalsed = 0;
  bool update_prev = true;
  while (current != NULL){
    if (next_phys(allocator, free_block) == current) {
      current->size += free_block->size + sizeof(block_header);
      allocator->available += sizeof(block_header);
      *free_block = *current;
      coalsed++;
      if (prev)
        prev->next = free_block;
      if (allocator->free_list_head == current)
        allocator->free_list_head = free_block;

      update_prev = false;
    }
    if (next_phys(allocator, current) == free_block) {
      current->size += free_block->size + sizeof(block_header);
      allocator->available += sizeof(block_header);
      free_block = current;
      coalsed++;
      update_prev = false;
    }

    if (coalsed >= 2)
      return true;


    if (update_prev)
      prev = current;
    else
      update_prev = true;
    current = current->next;
  }

  return coalsed > 0;
}

void dp_free(dp_alloc *allocator, void *ptr) {
  if (ptr == NULL || allocator == NULL) {
    return;
  }

  block_header *to_free =
      (block_header *)((uint8_t *)ptr - sizeof(block_header));

  if ((uint8_t *)to_free < allocator->buffer ||
      (uint8_t *)to_free >= allocator->buffer + allocator->buffer_size) {
    fprintf(stderr, "Error: Deallocating invalid pointer %p\n", ptr);
    return; // Invalid pointer
  }
  if (to_free->is_free) {
    fprintf(stderr, "Error: Double free detected for pointer %p\n", ptr);
    return; // Double free
  }

  to_free->is_free = true;
  allocator->available += to_free->size;
  if (!try_coalsce(allocator, to_free)) {
    to_free->next = allocator->free_list_head;
    allocator->free_list_head = to_free;
  }
}
