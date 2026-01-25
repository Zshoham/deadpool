#if DP_LOG

typedef struct dp_logger {
  void (*debug)(const char *fmt, ...);
  void (*info)(const char *fmt, ...);
  void (*warning)(const char *fmt, ...);
  void (*error)(const char *fmt, ...);
} dp_logger;

#define DP_DEBUG(alloc, ...)                                                                       \
  do {                                                                                             \
    alloc->logger.debug(__VA_ARGS__);                                                              \
  } while (0)
#define DP_INFO(alloc, ...)                                                                        \
  do {                                                                                             \
    alloc->logger.info(__VA_ARGS__);                                                               \
  } while (0)
#define DP_WARNING(alloc, ...)                                                                     \
  do {                                                                                             \
    alloc->logger.warning(__VA_ARGS__);                                                            \
  } while (0)
#define DP_ERROR(alloc, ...)                                                                       \
  do {                                                                                             \
    alloc->logger.error(__VA_ARGS__);                                                              \
  } while (0)

#else
#define DP_DEBUG(alloc, ...)   /*Logging Disabled*/
#define DP_INFO(alloc, ...)    /*Logging Disabled*/
#define DP_WARNING(alloc, ...) /*Logging Disabled*/
#define DP_ERROR(alloc, ...)   /*Logging Disabled*/

#endif
