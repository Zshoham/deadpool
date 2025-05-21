#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "allocator.h"

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

  // Best-fit search: Iterate through the free list to find the smallest block
  // that can accommodate the requested size.
  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;
  block_header *prev_best_fit = NULL; // Keep track of the block before best_fit
  block_header *best_fit = current; // Initialize best_fit to the head
  size_t min_fit = UINTPTR_MAX;     // Initialize min_fit to a large value

  do {
    // Calculate how much extra space this block would have if used for the current allocation.
    // This is the "fit". A smaller fit is better.
    size_t fit = current->size - size;
    if (fit < min_fit) {
      best_fit = current;
      prev_best_fit = prev;
      min_fit = fit;
    }
    // If we found a perfect fit (extra space is 0), no need to search further.
    if (min_fit == 0)
      break;
    prev = current;
    current = current->next;
  } while (current != NULL);

  // This condition checks if any suitable block was found.
  // If min_fit is still UINTPTR_MAX or larger than the best_fit block's size,
  // it means no block can satisfy the request (possibly due to underflow if size was huge).
  if (min_fit > best_fit->size)
    return NULL;

  // If the chosen best_fit block is too small to be split (i.e., the remaining
  // part would be smaller than a block_header), then allocate the entire block.
  if (min_fit < sizeof(block_header)) {
    // Remove best_fit from the free list
    if (best_fit == allocator->free_list_head) {
      allocator->free_list_head = best_fit->next;
    } else {
      prev_best_fit->next = best_fit->next;
    }
  } else {
    // Split the best_fit block. The new free block will start after the allocated portion.
    block_header *new_free_block =
        (block_header *)((uint8_t *)best_fit + required_size);
    new_free_block->size = best_fit->size - required_size; // Size of the remaining free part
    new_free_block->is_free = true;
    new_free_block->next = best_fit->next; // The new free block inherits best_fit's next

    // Update the free list to point to the new_free_block instead of best_fit
    if (best_fit == allocator->free_list_head) {
      allocator->free_list_head = new_free_block;
    } else {
      prev_best_fit->next = new_free_block;
    }
  }

  // The commented-out block below seems to be an alternative way of handling
  // the free list update when splitting, which is now handled by the logic above.
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

  // Configure the allocated block
  best_fit->size = size; // Set its size to the requested user size
  best_fit->is_free = false;
  best_fit->next = (block_header *)UINTPTR_MAX; // Mark as allocated
  allocator->available -= required_size;
  return (uint8_t *)best_fit + sizeof(block_header);
}

// Coalesce adjacent free blocks.
// This function iterates through the free list to find blocks that are
// physically adjacent to `free_block` and merges them.
static block_header* coalsce(dp_alloc *allocator, block_header *free_block) {
  block_header *current = allocator->free_list_head;
  block_header *prev = NULL;
  block_header *to_coalsce_left = NULL;  // Block physically before free_block
  block_header *to_coalsce_right = NULL; // Block physically after free_block

  // Iterate through the free list to find potential neighbors for coalescing.
  // Note: This search assumes the free list is not necessarily sorted by address.
  while (current != NULL) {
    // Check if `current` is physically right after `free_block`
    if (next_phys(allocator, free_block) == current) {
      to_coalsce_right = current;
      // Remove to_coalsce_right from the free list as it will be merged
      if (current == allocator->free_list_head){
        allocator->free_list_head = to_coalsce_right->next;
      }
      else {
        prev->next = to_coalsce_right->next;
      }
      current = to_coalsce_right->next; // Continue search from next block
      to_coalsce_right->next = (block_header*)UINTPTR_MAX; // Mark as no longer in free list for safety
      if (to_coalsce_left) // If we've found both neighbors, stop
        break;
      else
        continue; // Otherwise, continue searching for the other neighbor
    }
    // Check if `current` is physically right before `free_block`
    if (next_phys(allocator, current) == free_block) {
      to_coalsce_left = current;
      // Remove to_coalsce_left from the free list as it will be merged
      if (current == allocator->free_list_head) {
        allocator->free_list_head = to_coalsce_left->next;
      }
      else {
        prev->next = to_coalsce_left->next;
      }
      current = to_coalsce_left->next; // Continue search from next block
      to_coalsce_left->next = (block_header*)UINTPTR_MAX; // Mark as no longer in free list for safety
      if (to_coalsce_right) // If we've found both neighbors, stop
        break;
      else
        continue; // Otherwise, continue searching for the other neighbor
    }

    prev = current;
    current = current->next;
  }

  // If no adjacent free blocks were found, return the original block
  if (to_coalsce_left == NULL && to_coalsce_right == NULL)
    return free_block;

  // Merge with the left block if found
  if (to_coalsce_left != NULL) {
    to_coalsce_left->size += sizeof(block_header) + free_block->size;
    free_block = to_coalsce_left; // The merged block is now referenced by to_coalsce_left's header
  }

  // Merge with the right block if found
  // If free_block was merged with to_coalsce_left, free_block now points to to_coalsce_left.
  // The size is correctly updated.
  if (to_coalsce_right != NULL) {
    free_block->size += sizeof(block_header) + to_coalsce_right->size;
  }

  return free_block; // Return the header of the (potentially) coalesced block
}

int dp_free(dp_alloc *allocator, void *ptr) {
  if (ptr == NULL || allocator == NULL) {
    return 0; // Or an error code, depending on desired behavior for invalid args
  }

  // Get the header of the block to be freed
  block_header *to_free =
      (block_header *)((uint8_t *)ptr - sizeof(block_header));

  // Basic validation checks
  // Check if the block was marked as allocated (next == UINTPTR_MAX)
  if (to_free->next != (block_header *)UINTPTR_MAX) {
    fprintf(stderr, "Error: trying to free %p which is not a valid block\n",
            to_free);
    return 1; // Not a block allocated by this allocator or corrupted
  }
  // Check if the pointer is within the allocator's buffer range
  if ((uint8_t *)to_free < allocator->buffer ||
      (uint8_t *)to_free >= allocator->buffer + allocator->buffer_size) {
    fprintf(stderr, "Error: Deallocating invalid pointer %p\n", ptr);
    return 1; // Invalid pointer
  }
  // Check for double free
  if (to_free->is_free) {
    fprintf(stderr, "Error: Double free detected for pointer %p\n", ptr);
    fprintf(stderr, "Error: block size is %zu\n", to_free->size);
    return 1; // Double free
  }

  // Coalesce the block with adjacent free blocks
  to_free = coalsce(allocator, to_free);

  // Mark the block as free
  to_free->is_free = true;
  // Add the freed (and potentially coalesced) block's size back to available space.
  // Note: If coalesced, to_free->size already reflects the total size of the merged block.
  // The `sizeof(block_header)` is not added here because `to_free->size` is user data size.
  // However, the `allocator->available` calculation in `dp_malloc` subtracts `required_size`
  // which *includes* `sizeof(block_header)`. This needs to be consistent.
  // Let's assume `allocator->available` tracks the total available space including headers of free blocks.
  // When a block is allocated, `required_size` (user size + header) is subtracted.
  // When freed, `to_free->size` (user size of this block) + `sizeof(block_header)` should be added back.
  // If coalescing happened, `to_free->size` would be `left_user_size + header + free_block_user_size` or
  // `free_block_user_size + header + right_user_size` or
  // `left_user_size + header + free_block_user_size + header + right_user_size`.
  // The current `allocator->available += to_free->size` seems to only add back the user portion.
  // Let's adjust this to be consistent with dp_malloc.
  // When coalescing happens, the `sizeof(block_header)` for the block(s) being merged *into* another
  // effectively becomes available space.
  // `coalsce` updates `free_block->size` to include the `sizeof(block_header)` of the merged blocks.
  // Example: blockA, blockB, blockC. Free B. Coalesce with A. New A size = A.size + header + B.size.
  // So `to_free->size` after `coalsce` is the total new free space *including* the header of the block it merged.
  // The original `allocator->available` was reduced by `user_size + sizeof(block_header)`.
  // So, when freeing, we should add back `to_free->size + sizeof(block_header)`.
  // If `coalsce` has already incorporated the header sizes of merged blocks into `to_free->size`,
  // then `allocator->available += to_free->size` (if `to_free->size` is total new free space)
  // or `allocator->available += to_free->size + sizeof(block_header)` (if `to_free->size` is just user payload).

  // Re-evaluating:
  // dp_malloc: allocator->available -= required_size; (required_size = user_size + sizeof(header))
  // dp_free before this change: allocator->available += to_free->size; (to_free->size is user_size of the block being freed,
  //                                                                  or combined sizes if coalesced)
  // If coalesced, e.g., free_block merges with to_coalsce_left:
  //   to_coalsce_left->size += sizeof(block_header) + free_block->size;
  // Here, free_block->size is the user payload size. So the new size of the coalesced block
  // stored in to_coalsce_left->size is (old_left_payload + header_size + new_freed_payload).
  // So, `allocator->available += to_free->size` (where to_free is the resulting coalesced block)
  // correctly adds back the payload sizes and the headers that are now part of the larger free block.
  // The one header for the *final* coalesced block is still "in use" as a header for that free block.
  // So `allocator->available` should track the sum of (user_size + sizeof(block_header)) for all allocated blocks,
  // OR sum of (user_size) for all free blocks?
  // Let's assume `allocator->available` is the total free payload space.
  // Initial: available = buffer_size - sizeof(block_header) (payload of the first big free block)
  // Malloc: available -= (size + sizeof(block_header)) if not splitting, or available -= (size + sizeof(block_header)) if splitting. This seems wrong.
  // If splitting, best_fit becomes allocated. New free block size is `best_fit->size - required_size`.
  // `best_fit->size` was old payload. `required_size` is `user_size + header`.
  // So new free block payload is `old_payload - user_size - header`.
  // Change in available: `(old_payload) - (new_payload_of_remaining_free_block)`
  //                   `= old_payload - (old_payload - user_size - header) = user_size + header`.
  // So `allocator->available -= required_size` is correct.
  //
  // Free: `allocator->available += to_free->size` (where `to_free->size` is payload).
  // If coalesced: `left->size = left->size(payload) + sizeof(header) + freed->size(payload)`
  // So `to_free->size` becomes the new total payload of the coalesced block.
  // The increase in `available` should be `freed->size(payload) + sizeof(header)`.
  // This means `allocator->available += to_free->size(original_payload_of_block_being_freed) + sizeof(block_header)`.
  // The current `coalsce` function modifies `free_block->size` to be the new total size.
  // `allocator->available += to_free->size;` if `to_free->size` is the final payload of the (coalesced) block.
  // `allocator->available += (to_free->size + sizeof(block_header))` if `to_free->size` is user payload.
  // The line `allocator->available += to_free->size;` needs to be `allocator->available += to_free->size + sizeof(block_header);`
  // if `to_free->size` (before this line) is the user payload of the block *pointed to by ptr*.
  // Let's capture the original size before coalescing for available calculation.
  size_t freed_payload_size = to_free->size; // User payload size of the block being freed
  
  to_free = coalsce(allocator, to_free); // to_free might point to a different block if coalesced from left
                                         // and its ->size will be updated.

  to_free->is_free = true;
  // Add the size of the *user data* part of the freed block, plus its header, back to available.
  // If coalescing occurred, `coalsce` has already adjusted the sizes of the involved blocks
  // such that `to_free` (the final coalesced block) has its `size` field representing its new total payload.
  // The other headers from merged blocks are now part of this new payload.
  allocator->available += freed_payload_size + sizeof(block_header);


  // Add the (potentially coalesced) block to the head of the free list
  to_free->next = allocator->free_list_head;
  allocator->free_list_head = to_free;

  return 0;
}
