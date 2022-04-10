#include "mtrr.h"
#include "arch.h"

namespace hv {

// read MTRR data into a single structure
mtrr_data read_mtrr_data() {
  mtrr_data mtrrs;

  mtrrs.cap.flags      = __readmsr(IA32_MTRR_CAPABILITIES);
  mtrrs.def_type.flags = __readmsr(IA32_MTRR_DEF_TYPE);
  mtrrs.var_count      = 0;

  for (uint32_t i = 0; i < mtrrs.cap.variable_range_count; ++i) {
    ia32_mtrr_physmask_register mask;
    mask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);

    if (!mask.valid)
      continue;

    mtrrs.variable[mtrrs.var_count].mask = mask;
    mtrrs.variable[mtrrs.var_count].base.flags =
      __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);

    ++mtrrs.var_count;
  }

  return mtrrs;
}

// calculate the MTRR memory type for a single page
static uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t const pfn) {
  if (!mtrrs.def_type.mtrr_enable)
    return MEMORY_TYPE_UNCACHEABLE;

  // fixed range MTRRs
  if (pfn < 0x100 && mtrrs.cap.fixed_range_supported
      && mtrrs.def_type.fixed_range_mtrr_enable) {
    // TODO: implement this
    return MEMORY_TYPE_UNCACHEABLE;
  }

  uint8_t curr_mem_type = MEMORY_TYPE_INVALID;

  // variable-range MTRRs
  for (uint32_t i = 0; i < mtrrs.var_count; ++i) {
    auto const base = mtrrs.variable[i].base.page_frame_number;
    auto const mask = mtrrs.variable[i].mask.page_frame_number;

    // 3.11.11.2.3
    // essentially checking if the top part of the address (as specified
    // by the PHYSMASK) is equal to the top part of the PHYSBASE.
    if ((pfn & mask) == (base & mask)) {
      auto const type = static_cast<uint8_t>(mtrrs.variable[i].base.type);

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
    return mtrrs.def_type.default_memory_type;

  return curr_mem_type;
}

// calculate the MTRR memory type for the given physical memory range
uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t address, uint64_t size) {
  // base address must be on atleast a 4KB boundary
  address &= ~0xFFFull;

  // minimum range size is 4KB
  size = (size + 0xFFF) & ~0xFFFull;

  uint8_t curr_mem_type = MEMORY_TYPE_INVALID;

  for (uint64_t curr = address; curr < address + size; curr += 0x1000) {
    auto const type = calc_mtrr_mem_type(mtrrs, curr >> 12);

    if (type == MEMORY_TYPE_UNCACHEABLE)
      return type;

    // use the worse memory type between the two
    if (type < curr_mem_type)
      curr_mem_type = type;
  }

  if (curr_mem_type == MEMORY_TYPE_INVALID)
    return MEMORY_TYPE_UNCACHEABLE;

  return curr_mem_type;
}

} // namespace hv

