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

// represents a 4-level virtual address
union pml4_virtual_address {
  void const* address;
  struct {
    uint64_t offset   : 12;
    uint64_t pt_idx   : 9;
    uint64_t pd_idx   : 9;
    uint64_t pdpt_idx : 9;
    uint64_t pml4_idx : 9;
  };
};

// cache MTRR data into a single structure
mtrr_data read_mtrr_data();

// calculate the MTRR memory type for the given physical memory range
uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t address, uint64_t size);

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(cr3 guest_cr3, void* guest_virtual_address, size_t* offset_to_next_page = nullptr);

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(void* guest_virtual_address, size_t* offset_to_next_page = nullptr);

} // namespace hv

