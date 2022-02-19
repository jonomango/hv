#include "page-tables.h"
#include "hv.h"
#include "mm.h"

namespace hv {

// initialize the host page tables
void prepare_host_page_tables(host_page_tables& pt) {
  memset(&pt, 0, sizeof(pt));

  // TODO: perform a deep copy instead of a shallow one (only for the memory
  //   ranges that our hypervisor code resides in)

  auto const system_pml4 = static_cast<pml4e_64*>(get_virtual(
    ghv.system_cr3.address_of_page_directory << 12));

  // copy the top half of the System pml4 (a.k.a. the kernel address space)
  memcpy(&pt.pml4[256], &system_pml4[256], sizeof(pml4e_64) * 256);
}

} // namespace hv
