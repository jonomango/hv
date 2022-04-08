#pragma once

#include <ia32.hpp>

namespace hv {

// number of PDs in the EPT paging structures
inline constexpr size_t ept_pd_count = 64;
inline constexpr size_t ept_free_page_count = 10;
inline constexpr size_t ept_hook_count = 10;

struct vcpu_ept_data {
  // EPT PML4
  alignas(0x1000) ept_pml4e pml4[512];

  // EPT PDPT - a single one covers 512GB of physical memory
  alignas(0x1000) ept_pdpte pdpt[512];
  static_assert(ept_pd_count <= 512, "Only 512 EPT PDs are supported!");

  // an array of EPT PDs - each PD covers 1GB
  union {
    alignas(0x1000) ept_pde     pds[ept_pd_count][512];
    alignas(0x1000) ept_pde_2mb pds_2mb[ept_pd_count][512];
  };

  // free pages that can be used to split PDEs or for other purposes
  alignas(0x1000) uint8_t free_pages[ept_free_page_count][0x1000];

  // an array of PFNs that point to each free page in the free page array
  uint64_t free_page_pfns[ept_free_page_count];

  // # of free pages that are currently in use
  size_t num_used_free_pages;

  struct {
    ept_pte* pte;
    uint64_t orig_pfn;
    uint64_t exec_pfn;
  } ept_hooks[10];

  // # of EPT hooks that are installed
  size_t num_ept_hooks;
};

// identity-map the EPT paging structures
void prepare_ept(vcpu_ept_data& ept);

// update the memory types in the EPT paging structures based on the MTRRs.
// this function should only be called from root-mode during vmx-operation.
void update_ept_memory_type(vcpu_ept_data& ept);

// set the memory type in every EPT paging structure to the specified value
void set_ept_memory_type(vcpu_ept_data& ept, uint8_t memory_type);

// get the corresponding EPT PDPTE for a given physical address
ept_pdpte* get_ept_pdpte(vcpu_ept_data& ept, uint64_t physical_address);

// get the corresponding EPT PDE for a given physical address
ept_pde* get_ept_pde(vcpu_ept_data& ept, uint64_t physical_address);

// get the corresponding EPT PTE for a given physical address
ept_pte* get_ept_pte(vcpu_ept_data& ept, uint64_t physical_address, bool force_split = false);

// split a 2MB EPT PDE so that it points to an EPT PT
void split_ept_pde(vcpu_ept_data& ept, ept_pde_2mb* pde_2mb);

} // namespace hv

