#include "mm.h"

#include "arch.h"

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

// translate a virtual address to its physical address
uint64_t get_physical(void* const virt_addr) {
  return MmGetPhysicalAddress(virt_addr).QuadPart;
}

// translate a physical address to its virtual address
void* get_virtual(uint64_t const phys_addr) {
  PHYSICAL_ADDRESS phys;
  phys.QuadPart = phys_addr;
  return MmGetVirtualForPhysical(phys);
}

// calculate the MTRR memory type for a single page
static uint8_t calc_mtrr_mem_type(uint64_t const pfn) {
  ia32_mtrr_def_type_register mtrr_def_type;
  mtrr_def_type.flags = __readmsr(IA32_MTRR_DEF_TYPE);

  if (!mtrr_def_type.mtrr_enable)
    return MEMORY_TYPE_UNCACHEABLE;

  ia32_mtrr_capabilities_register mtrr_cap;
  mtrr_cap.flags = __readmsr(IA32_MTRR_CAPABILITIES);

  // fixed range MTRRs
  if (pfn < 0x100 && mtrr_cap.fixed_range_supported
      && mtrr_def_type.fixed_range_mtrr_enable) {
    // TODO: implement this
    return MEMORY_TYPE_UNCACHEABLE;
  }

  uint8_t curr_mem_type = MEMORY_TYPE_INVALID;

  // variable-range MTRRs
  for (uint32_t i = 0; i < mtrr_cap.variable_range_count; ++i) {
    ia32_mtrr_physmask_register phys_mask;
    phys_mask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);

    if (!phys_mask.valid)
      continue;

    ia32_mtrr_physbase_register phys_base;
    phys_base.flags = __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);

    auto const base = phys_base.page_frame_number;
    auto const mask = phys_mask.page_frame_number;

    // 3.11.11.2.3
    // essentially checking if the top part of the address (as specified
    // by the PHYSMASK) is equal to the top part of the PHYSBASE.
    if ((pfn & mask) == (base & mask)) {
      auto const type = static_cast<uint8_t>(phys_base.type);

      // UC takes precedence over everything
      if (type == MEMORY_TYPE_UNCACHEABLE)
        return MEMORY_TYPE_UNCACHEABLE;

      // this works for WT and WB, which is the only other "defined" overlap scenario
      if (type < curr_mem_type)
        curr_mem_type = type;
    }
  }

  // no MTRR covers the specified address
  if (curr_mem_type == MEMORY_TYPE_INVALID)
    return mtrr_def_type.default_memory_type;

  return curr_mem_type;
}

// calculate the MTRR memory type for the given physical memory range
uint8_t calc_mtrr_mem_type(uint64_t address, uint64_t size) {
  // base address must be on atleast a 4KB boundary
  address &= ~0xFFFull;

  // minimum range size is 4KB
  size = (size + 0xFFF) & ~0xFFFull;

  uint8_t curr_mem_type = MEMORY_TYPE_INVALID;

  for (uint64_t curr = address; curr < address + size; curr += 0x1000) {
    auto const type = calc_mtrr_mem_type(curr >> 12);

    if (type == MEMORY_TYPE_UNCACHEABLE)
      return type;

    // use the worse memory type between the two
    if (type < curr_mem_type)
      curr_mem_type = type;
  }

  NT_ASSERT(curr_mem_type != MEMORY_TYPE_INVALID);

  return curr_mem_type;
}

} // namespace hv
