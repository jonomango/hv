#pragma once

#include <ia32.hpp>

namespace hv {

// the first 128GB of physical memory is mapped to this pml4 entry
constexpr inline uint64_t host_physical_memory_pml4_idx = 255;

struct host_page_tables {
  // array of PML4 entries that point to a PDPT
  alignas(0x1000) pml4e_64 pml4[512];

  // PDPT for mapping physical memory
  alignas(0x1000) pdpte_64 phys_pdpt[512];

  // PDs for mapping physical memory
  alignas(0x1000) pde_2mb_64 phys_pds[128][512];
};

// initialize the host page tables
void prepare_host_page_tables();

} // namespace hv

