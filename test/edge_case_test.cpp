#include "test_common.hpp"

// Tests for uncovered branches in allocator.c

// dp_init edge cases (lines 24, 30)
class DPInitEdgeCaseTest : public ::testing::Test {
protected:
  dp_alloc allocator;
  std::array<uint8_t, 1024> buffer;
};

TEST_F(DPInitEdgeCaseTest, NullBuffer) {
  bool result = dp_init(
      &allocator, nullptr,
      1024 IF_DP_LOG(
          ,
          {.debug = test_debug, .info = test_info, .warning = test_warning, .error = test_error}));
  ASSERT_FALSE(result);
}

TEST_F(DPInitEdgeCaseTest, BufferSizeTooSmallForHeader) {
  bool result = dp_init(&allocator, buffer.data(),
                        sizeof(block_header) - 1 IF_DP_LOG(, {.debug = test_debug,
                                                              .info = test_info,
                                                              .warning = test_warning,
                                                              .error = test_error}));
  ASSERT_FALSE(result);
}

TEST_F(DPInitEdgeCaseTest, BufferSizeExactlyHeader) {
  bool result = dp_init(
      &allocator, buffer.data(),
      sizeof(block_header) IF_DP_LOG(
          ,
          {.debug = test_debug, .info = test_info, .warning = test_warning, .error = test_error}));
  ASSERT_FALSE(result);
}

TEST_F(DPInitEdgeCaseTest, BufferSizeTooSmallAfterAlignment) {
  uint8_t misaligned_buffer[sizeof(block_header) + DEFAULT_ALIGN];
  uint8_t *misaligned_ptr = misaligned_buffer + 1;
  bool result = dp_init(&allocator, misaligned_ptr,
                        sizeof(block_header) + 1 IF_DP_LOG(, {.debug = test_debug,
                                                              .info = test_info,
                                                              .warning = test_warning,
                                                              .error = test_error}));
  ASSERT_FALSE(result);
}

// dp_malloc edge cases (line 71)
TEST_F(DPAllocatorTest, MallocWithNullAllocator) {
  void *ptr = dp_malloc(nullptr, 100);
  ASSERT_EQ(nullptr, ptr);
}

TEST_F(DPAllocatorTest, FreeNullPtr) {
  dp_free(&allocator, nullptr); // Should handle this gracefully
}

TEST_F(DPAllocatorTest, FreePointerOutsideBuffer) {
  uint8_t external_buffer[256];
  void *external_ptr = external_buffer + sizeof(block_header) + DEFAULT_ALIGN;
  int result = dp_free(&allocator, external_ptr);
  ASSERT_EQ(1, result);
}

TEST_F(DPAllocatorTest, FreeInvalidBlockWithNonNullNext) {
  void *ptr = dp_malloc(&allocator, 64);
  ASSERT_NE(nullptr, ptr);

  uint8_t offset = *((uint8_t *)ptr - 1);
  block_header *header = (block_header *)((uint8_t *)ptr - offset - sizeof(block_header));
  header->next = (block_header *)0xDEADBEEF;

  int result = dp_free(&allocator, ptr);
  ASSERT_EQ(1, result);

  header->next = nullptr;
  header->is_free = false;
  ASSERT_EQ(0, dp_free(&allocator, ptr));
  allocated.clear();
}

TEST_F(DPAllocatorTest, DoubleFree) {
  void *ptr = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, ptr);

  dp_free(&allocator, ptr);
  dp_free(&allocator, ptr); // Should handle this gracefully

  // Should still be able to allocate
  void *new_ptr = dp_malloc(&allocator, 100);
  ASSERT_NE(nullptr, new_ptr);
}

// dp_get_fragmentation edge cases (lines 278, 292)
#if DP_STATS
TEST_F(DPAllocatorTest, FragmentationWithNoFreeBlocks) {
  void *ptr = dp_malloc(&allocator, BUFFER_SIZE - 2 * sizeof(block_header));
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(nullptr, allocator.free_list_head);

  float frag = dp_get_fragmentation(&allocator);
  ASSERT_FLOAT_EQ(0.0f, frag);

  ASSERT_EQ(0, dp_free(&allocator, ptr));
  allocated.clear();
}
#endif
