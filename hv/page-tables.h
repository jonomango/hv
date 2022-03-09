#pragma once

#include <ia32.hpp>

namespace hv {

// how much of physical memory to map into the host address-space
inline constexpr size_t host_physical_memory_pd_count = 64;

// physical memory is directly mapped to this pml4 entry
inline constexpr uint64_t host_physical_memory_pml4_idx = 255;

// directly access physical memory by using [base + offset]
inline uint8_t* const host_physical_memory_base = reinterpret_cast<uint8_t*>(
  host_physical_memory_pml4_idx << (9 + 9 + 9 + 12));

struct host_page_tables {
  // array of PML4 entries that point to a PDPT
  alignas(0x1000) pml4e_64 pml4[512];

  // PDPT for mapping physical memory
  alignas(0x1000) pdpte_64 phys_pdpt[512];

  // PDs for mapping physical memory
  alignas(0x1000) pde_2mb_64 phys_pds[host_physical_memory_pd_count][512];
};

// initialize the host page tables
void prepare_host_page_tables();

} // namespace hv

