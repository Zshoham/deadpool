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
    // we have a check after the loop to catch underflows.
    size_t fit = current->size - size;
    if (fit < min_fit) {
      best_fit = current;
      prev_best_fit = prev;
      min_fit = fit;
    }
    if (min_fit == 0)
      break;
    prev = current;
    current = current->next;
  } while (current != NULL);

  if (min_fit > best_fit->size)
    return NULL;

  // fprintf(stderr, "Info: trying to allocate bf=%p, sz=%zu bfn=%p hd=%p\n",
  //         best_fit, best_fit->size, best_fit->next, allocator->free_list_head);

  if (min_fit < sizeof(block_header)) {
    if (best_fit == allocator->free_list_head) {
      allocator->free_list_head = best_fit->next;
    } else {
      prev_best_fit->next = best_fit->next;
    }
  } else {
    // TODO: need to make sure we actually have enough space for this.
    block_header *new_best_fit =
        (block_header *)((uint8_t *)best_fit + required_size);
    // fprintf(stderr, "Info: rearanging stuff nbf=%p\n", new_best_fit);
    new_best_fit->size = best_fit->size - required_size;
    new_best_fit->is_free = true;
    new_best_fit->next = best_fit->next;
    if (best_fit == allocator->free_list_head) {
      allocator->free_list_head = new_best_fit;
    } else {
      prev_best_fit->next = new_best_fit;
    }
  }

  // if (prev_best_fit) {
  //   prev_best_fit->next = best_fit->next;
  // } else {
  //   block_header *new_free_head =
  //       (block_header *)((uint8_t *)best_fit + required_size);
  //   new_free_head->next = allocator->free_list_head->next;
  //   new_free_head->size = best_fit->size - required_size;
  //   new_free_head->is_free = true;
  //   allocator->free_list_head = new_free_head;
  // }

  best_fit->size = size;
  best_fit->is_free = false;
  best_fit->next = (block_header *)UINTPTR_MAX;
  allocator->available -= required_size;
  // fprintf(stderr, "Info: allocated block at %p (sz=%zu, hd=%p)\n", best_fit,
          // best_fit->size, allocator->free_list_head);
  return (uint8_t *)best_fit + sizeof(block_header);
}

static block_header* coalsce(dp_alloc *allocator, block_header *free_block) {
  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;
  block_header *to_coalsce_left = NULL;
  block_header *to_coalsce_right = NULL;

  while (current != NULL) {
    if (next_phys(allocator, free_block) == current) {
      // fprintf(stderr, "Info: coalscing(left) (free) %p-%p with %p-%p\n",
      //         free_block, current, current, next_phys(allocator, current));
      to_coalsce_right = current;
      if (current == allocator->free_list_head){
        allocator->free_list_head = to_coalsce_right->next;
      }
      else {
        prev->next = to_coalsce_right->next;
      }
      current = to_coalsce_right->next;
      to_coalsce_right->next = (block_header*)UINTPTR_MAX;
      if (to_coalsce_left)
        break;
      else
        continue;
    }
    if (next_phys(allocator, current) == free_block) {
      // fprintf(stderr, "Info: coalscing(right) (free) %p-%p with %p-%p\n",
      //         free_block, next_phys(allocator, free_block), current,
      //         free_block);
      to_coalsce_left = current;
      if (current == allocator->free_list_head) {
        allocator->free_list_head = to_coalsce_left->next;
      }
      else {
        prev->next = to_coalsce_left->next;
      }
      current = to_coalsce_left->next;
      to_coalsce_left->next = (block_header*)UINTPTR_MAX;
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
    to_coalsce_left->size += sizeof(block_header) + free_block->size;
    free_block = to_coalsce_left;
  }

  if (to_coalsce_right != NULL) {
    free_block->size += sizeof(block_header) + to_coalsce_right->size;
  }

  // fprintf(stderr, "Info: Successfull coalscence\n");
  return free_block;
}

int dp_free(dp_alloc *allocator, void *ptr) {
  if (ptr == NULL || allocator == NULL) {
    return 0;
  }

  block_header *to_free =
      (block_header *)((uint8_t *)ptr - sizeof(block_header));

  // fprintf(stderr, "Info: Freeing block at %p\n", to_free);
  if (to_free->next != (block_header *)UINTPTR_MAX) {
    fprintf(stderr, "Error: trying to free %p which is not a valid block\n",
            to_free);
    return 1;
  }
  if ((uint8_t *)to_free < allocator->buffer ||
      (uint8_t *)to_free >= allocator->buffer + allocator->buffer_size) {
    fprintf(stderr, "Error: Deallocating invalid pointer %p\n", ptr);
    return 1; // Invalid pointer
  }
  if (to_free->is_free) {
    fprintf(stderr, "Error: Double free detected for pointer %p\n", ptr);
    fprintf(stderr, "Error: block size is %zu\n", to_free->size);
    return 1; // Double free
  }

  to_free = coalsce(allocator, to_free);
  to_free->is_free = true;
  allocator->available += to_free->size;
  to_free->next = allocator->free_list_head;
  allocator->free_list_head = to_free;

  // fprintf(stderr, "Info: free Successfull now  checking for errors\n");
  // block_header *current = allocator->free_list_head;
  // uint32_t circle_lengh = 0;
  // while (current != NULL) {
  //   // if (current == to_free) {
  //   //   fprintf(stderr, "Error: found block %p in free list after it was freed\n",
  //   //           to_free);
  //   // }
  //   if (current == (block_header*)UINTPTR_MAX) {
  //     fprintf(stderr, "Error: free list is corrupted after freeing %p\n", to_free);
  //   }
  //
  //   current = current->next;
  //   circle_lengh += 1;
  //   if (current == allocator->free_list_head) {
  //     fprintf(stderr, "Error: free list is cirular, with length %u.\n", circle_lengh);
  //     return 1;
  //   }
  // }

  // fprintf(stderr, "Info: freed block at %p, free list has %u blocks\n", to_free, circle_lengh);
  return 0;
}
