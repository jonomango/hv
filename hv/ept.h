#pragma once

#include <ia32.hpp>

namespace hv {

// number of PDs in the EPT paging structures
inline constexpr size_t ept_pd_count = 64;

struct vcpu_ept_data {
  // EPT PML4
  alignas(0x1000) ept_pml4 pml4[512];

  // EPT PDPT - a single one covers 512GB of physical memory
  alignas(0x1000) epdpte pdpt[512];
  static_assert(ept_pd_count <= 512, "Only 512 EPT PDs are supported!");

  // an array of EPT PDs - each PD covers 1GB
  union {
    alignas(0x1000) epde     pds[ept_pd_count][512];
    alignas(0x1000) epde_2mb pds_2mb[ept_pd_count][512];
  };
};

// identity-map the EPT paging structures
void prepare_ept(vcpu_ept_data& ept);

} // namespace hv

