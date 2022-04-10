#include "mm.h"
#include "arch.h"
#include "page-tables.h"
#include "vmx.h"

namespace hv {

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(cr3 const guest_cr3, void* const guest_virtual_address, size_t* const offset_to_next_page) {
  if (offset_to_next_page)
    *offset_to_next_page = 0;

  pml4_virtual_address const vaddr = { guest_virtual_address };

  // guest PML4
  auto const pml4 = reinterpret_cast<pml4e_64*>(host_physical_memory_base
    + (guest_cr3.address_of_page_directory << 12));
  auto const pml4e = pml4[vaddr.pml4_idx];

  if (!pml4e.present)
    return nullptr;

  // guest PDPT
  auto const pdpt = reinterpret_cast<pdpte_64*>(host_physical_memory_base
    + (pml4e.page_frame_number << 12));
  auto const pdpte = pdpt[vaddr.pdpt_idx];

  if (!pdpte.present)
    return nullptr;

  if (pdpte.large_page) {
    pdpte_1gb_64 pdpte_1gb;
    pdpte_1gb.flags = pdpte.flags;

    auto const offset = (vaddr.pd_idx << 18) + (vaddr.pt_idx << 9) + vaddr.offset;

    // 1GB
    if (offset_to_next_page)
      *offset_to_next_page = 0x40000000 - offset;

    return host_physical_memory_base + (pdpte_1gb.page_frame_number << 30) + offset;
  }

  // guest PD
  auto const pd = reinterpret_cast<pde_64*>(host_physical_memory_base
    + (pdpte.page_frame_number << 12));
  auto const pde = pd[vaddr.pd_idx];

  if (!pde.present)
    return nullptr;

  if (pde.large_page) {
    pde_2mb_64 pde_2mb;
    pde_2mb.flags = pde.flags;

    auto const offset = (vaddr.pt_idx << 9) + vaddr.offset;

    // 2MB page
    if (offset_to_next_page)
      *offset_to_next_page = 0x200000 - offset;

    return host_physical_memory_base + (pde_2mb.page_frame_number << 21) + offset;
  }

  // guest PT
  auto const pt = reinterpret_cast<pte_64*>(host_physical_memory_base
    + (pde.page_frame_number << 12));
  auto const pte = pt[vaddr.pt_idx];

  if (!pte.present)
    return nullptr;

  // 4KB page
  if (offset_to_next_page)
    *offset_to_next_page = 0x1000 - vaddr.offset;

  return host_physical_memory_base + (pte.page_frame_number << 12) + vaddr.offset;
}

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(void* const guest_virtual_address, size_t* const offset_to_next_page) {
  cr3 guest_cr3;
  guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);
  return gva2hva(guest_cr3, guest_virtual_address, offset_to_next_page);
}

} // namespace hv
