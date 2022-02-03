#pragma once

#include <ntddk.h>
#include <ia32.hpp>

namespace hv {

// allocate enough memory for the specified type
// note, this does not construct the object
template <typename T>
T* alloc(POOL_TYPE const type = NonPagedPoolNx) {
  return static_cast<T*>(alloc_aligned(sizeof(T), alignof(T), type));
}

// allocate system memory that is aligned to 16 bytes
void* alloc(size_t size, POOL_TYPE type = NonPagedPoolNx);

// allocate aligned system memory
void* alloc_aligned(size_t size, size_t alignment, POOL_TYPE type = NonPagedPoolNx);

// free previously allocated memory
void free(void* address);

// translate a virtual address to its physical address
uint64_t get_physical(void* virt_addr);

// translate a physical address to its virtual address
void* get_virtual(uint64_t phys_addr);

} // namespace hv

