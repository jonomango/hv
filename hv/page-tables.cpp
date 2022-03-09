#include "page-tables.h"
#include "vcpu.h"
#include "hv.h"
#include "mm.h"

namespace hv {

// map the first 128GB of physical memory into the specified PML4E
static void map_physical_memory(host_page_tables& pt) {
  auto& pml4e = pt.pml4[host_physical_memory_pml4_idx];
  pml4e.flags                    = 0;
  pml4e.present                  = 1;
  pml4e.write                    = 1;
  pml4e.supervisor               = 0;
  pml4e.page_level_write_through = 0;
  pml4e.page_level_cache_disable = 0;
  pml4e.accessed                 = 0;
  pml4e.execute_disable          = 0;
  pml4e.page_frame_number = MmGetPhysicalAddress(&pt.phys_pdpt).QuadPart >> 12;

  // TODO: add support for 1GB pages
  // TODO: check if 2MB pages are supported (pretty much always are)

  for (uint64_t i = 0; i < 128; ++i) {
    auto& pdpte = pt.phys_pdpt[i];
    pdpte.flags                    = 0;
    pdpte.present                  = 1;
    pdpte.write                    = 1;
    pdpte.supervisor               = 0;
    pdpte.page_level_write_through = 0;
    pdpte.page_level_cache_disable = 0;
    pdpte.accessed                 = 0;
    pdpte.execute_disable          = 0;
    pdpte.page_frame_number = MmGetPhysicalAddress(&pt.phys_pds[i]).QuadPart >> 12;

    for (uint64_t j = 0; j < 512; ++j) {
      auto& pde = pt.phys_pds[i][j];
      pde.flags                    = 0;
      pde.present                  = 1;
      pde.write                    = 1;
      pde.supervisor               = 0;
      pde.page_level_write_through = 0;
      pde.page_level_cache_disable = 0;
      pde.accessed                 = 0;
      pde.dirty                    = 0;
      pde.large_page               = 1;
      pde.global                   = 0;
      pde.pat                      = 0;
      pde.execute_disable          = 0;
      pde.page_frame_number = (i << 9) + j;
    }
  }
}

// map shared guest pages into the host page tables
static void map_shared_memory(host_page_tables& pt, shared_memory_region const& smr) {
  PHYSICAL_ADDRESS pml4_address;
  pml4_address.QuadPart = ghv.system_cr3.address_of_page_directory << 12;

  // kernel PML4 address
  auto const system_pml4 = static_cast<pml4e_64*>(MmGetVirtualForPhysical(pml4_address));

  for (size_t r = 0; r < smr.count; ++r) {
    auto [base, size] = smr.regions[r];

    pml4_virtual_address vcurr = { base };

    // TODO: check for overflow
    while (vcurr.address < base + size + 0xFFF) {

      // go to next page
      reinterpret_cast<uint8_t*&>(vcurr) += 0x1000;
    }
  }

  // copy the top half of the System pml4 (a.k.a. the kernel address space)
  memcpy(&pt.pml4[256], &system_pml4[256], sizeof(pml4e_64) * 256);
}

// initialize the host page tables
void prepare_host_page_tables() {
  auto& pt = ghv.host_page_tables;
  memset(&pt, 0, sizeof(pt));

  // map all of physical memory into our address space
  map_physical_memory(pt);

  // map shared memory into our address space
  map_shared_memory(pt, ghv.shared_memory_region);
}

} // namespace hv
