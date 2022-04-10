#pragma once

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

  // number of valid variable-range MTRRs
  size_t var_count;
};

// read MTRR data into a single structure
mtrr_data read_mtrr_data();

// calculate the MTRR memory type for the given physical memory range
uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t address, uint64_t size);

} // namespace hv

