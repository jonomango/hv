#pragma once

#include <ntddk.h>

namespace hv {

// allocate system memory that is aligned to 16 bytes
void* alloc(size_t size, POOL_TYPE type = NonPagedPoolNx);

// allocate enough memory for the specified type
// note, this does not construct the object
template <typename T>
T* alloc(POOL_TYPE const type = NonPagedPoolNx) {
  return reinterpret_cast<T*>(alloc_aligned(sizeof(T), alignof(T), type));
}

// allocate aligned system memory
void* alloc_aligned(size_t size, size_t alignment, POOL_TYPE type = NonPagedPoolNx);

// free previously allocated memory
void free(void* address);

} // namespace hv

