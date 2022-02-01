#include "mm.h"

namespace hv {

// allocate system memory that is aligned to 16 bytes
void* alloc(size_t const size, POOL_TYPE const type) {
  return alloc_aligned(size, 16, type);
}

// allocate aligned system memory
void* alloc_aligned(size_t const size,
    size_t const alignment, POOL_TYPE const type) {
  NT_ASSERT(alignment > 0 && alignment <= PAGE_SIZE);

  if (size >= PAGE_SIZE || alignment <= 16)
    return ExAllocatePoolWithTag(type, size, 'frog');

  // page aligned
  return ExAllocatePoolWithTag(type, PAGE_SIZE, 'frog');
}

// free previously allocated memory
void free(void* const address) {
  NT_ASSERT(address != nullptr);
  ExFreePoolWithTag(address, 'frog');
}

} // namespace hv
