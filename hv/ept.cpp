#include "ept.h"
#include "mm.h"

namespace hv {

// identity-map the EPT paging structures
void prepare_ept(vcpu_ept_data& ept) {
  memset(&ept, 0, sizeof(ept));

  DbgPrint("[hv] Preparing EPT.\n");

  // setup the first PML4E so that it points to our PDPT
  auto& pml4e             = ept.pml4[0];
  pml4e.flags             = 0;
  pml4e.read_access       = 1;
  pml4e.write_access      = 1;
  pml4e.execute_access    = 1;
  pml4e.accessed          = 0;
  pml4e.user_mode_execute = 1;
  pml4e.page_frame_number = get_physical(&ept.pdpt) >> 12;

  for (size_t i = 0; i < ept_pd_count; ++i) {
    // point each PDPTE to the corresponding PD
    auto& pdpte             = ept.pdpt[i];
    pdpte.flags             = 0;
    pdpte.read_access       = 1;
    pdpte.write_access      = 1;
    pdpte.execute_access    = 1;
    pdpte.accessed          = 0;
    pdpte.user_mode_execute = 1;
    pdpte.page_frame_number = get_physical(&ept.pds[i]);

    for (size_t j = 0; j < 512; ++j) {
      auto const pfn = (i << 9) + j;

      // identity-map every GPA to the corresponding HPA
      auto& pde             = ept.pds_2mb[i][j];
      pde.flags             = 0;
      pde.read_access       = 1;
      pde.write_access      = 1;
      pde.execute_access    = 1;
      pde.memory_type       = calc_mtrr_mem_type(pfn << (9 + 12), 0x1000 << 9);
      pde.ignore_pat        = 0;
      pde.large_page        = 1;
      pde.accessed          = 0;
      pde.dirty             = 0;
      pde.user_mode_execute = 1;
      pde.suppress_ve       = 0;
      pde.page_frame_number = pfn;
    }
  }
}

} // namespace hv

