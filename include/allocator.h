#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <config.h>

#ifdef __cplusplus
extern "C" {
#endif

#if DP_LOG
typedef struct dp_logger {
   int (*debug)(const char *fmt, ...);
   int (*info)(const char *fmt, ...);
   int (*warning)(const char *fmt, ...);
   int (*error)(const char *fmt, ...);
} dp_logger;

#define DP_DEBUG(alloc, ...) do { alloc->logger.debug(__VA_ARGS__); } while (0)
#define DP_INFO(alloc, ...) do { alloc->logger.debug(__VA_ARGS__); } while (0)
#define DP_WARNING(alloc, ...) do { alloc->logger.debug(__VA_ARGS__); } while (0)
#define DP_ERROR(alloc, ...) do { alloc->logger.debug(__VA_ARGS__); } while (0)

#elif
#define EMPTY_MACRO do {} while (0)

#define DP_DEBUG(alloc) EMPTY_MACRO
#define DP_INFO(alloc) EMPTY_MACRO
#define DP_WARNING(alloc) EMPTY_MACRO
#define DP_ERROR(alloc) EMPTY_MACRO

#endif


typedef struct block_header {
    struct block_header* next;
    size_t size;
    bool is_free;
} block_header;

typedef struct dp_alloc {
    uint8_t* buffer;
    size_t buffer_size;
    size_t available;
    block_header* free_list_head;

    #if DP_LOG
    dp_logger logger;
    #endif
} dp_alloc;

bool dp_init(dp_alloc* allocator, void* buffer, size_t buffer_size);
void* dp_malloc(dp_alloc* allocator, size_t size);
int dp_free(dp_alloc* allocator, void* ptr);

#ifdef __cplusplus
}
#endif

#endif // ALLOCATOR_H
