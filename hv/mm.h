#pragma once

#include <ntddk.h>
#include <ia32.hpp>

namespace hv {

struct mtrr_data {
  ia32_mtrr_capabilities_register cap;
  ia32_mtrr_def_type_register def_type;

  // fixed-range MTRRs
  struct {
    // TODO: implement
  } fixed;

  // variable-range MTRRs
  struct {
    ia32_mtrr_physbase_register base;
    ia32_mtrr_physmask_register mask;
  } variable[64];

  // number of variable-range MTRRs
  size_t var_count;
};

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

// cache MTRR data into a single structure
mtrr_data read_mtrr_data();

// calculate the MTRR memory type for the given physical memory range
uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t address, uint64_t size);

} // namespace hv

