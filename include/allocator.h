#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <config_macros.h>

#ifdef __cplusplus
extern "C" {
#endif

#if DP_LOG

typedef struct dp_logger {
   void (*debug)(const char *fmt, ...);
   void (*info)(const char *fmt, ...);
   void (*warning)(const char *fmt, ...);
   void (*error)(const char *fmt, ...);
} dp_logger;

#define DP_DEBUG(alloc, ...) do { alloc->logger.debug(__VA_ARGS__); } while (0)
#define DP_INFO(alloc, ...) do { alloc->logger.info(__VA_ARGS__); } while (0)
#define DP_WARNING(alloc, ...) do { alloc->logger.warning(__VA_ARGS__); } while (0)
#define DP_ERROR(alloc, ...) do { alloc->logger.error(__VA_ARGS__); } while (0)

#else
#define DP_DEBUG(alloc, ...) /*Logging Disabled*/
#define DP_INFO(alloc, ...) /*Logging Disabled*/
#define DP_WARNING(alloc, ...) /*Logging Disabled*/
#define DP_ERROR(alloc, ...) /*Logging Disabled*/

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

    IF_DP_LOG(dp_logger logger;)
} dp_alloc;

bool dp_init(dp_alloc* allocator, void* buffer, size_t buffer_size IF_DP_LOG(, dp_logger logger));
void* dp_malloc(dp_alloc* allocator, size_t size);
int dp_free(dp_alloc* allocator, void* ptr);

#ifdef __cplusplus
}
#endif

#endif // ALLOCATOR_H
