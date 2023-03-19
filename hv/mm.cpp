#include "mm.h"
#include "arch.h"
#include "page-tables.h"
#include "vmx.h"
#include "exception-routines.h"
#include "logger.h"

namespace hv {

// translate a GVA to a GPA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the GPA in order to modify the GVA.
uint64_t gva2gpa(cr3 const guest_cr3, void* const guest_virtual_address, size_t* const offset_to_next_page) {
  if (offset_to_next_page)
    *offset_to_next_page = 0;

  pml4_virtual_address const vaddr = { guest_virtual_address };

  // guest PML4
  auto const pml4 = reinterpret_cast<pml4e_64*>(host_physical_memory_base
    + (guest_cr3.address_of_page_directory << 12));
  auto const pml4e = pml4[vaddr.pml4_idx];

  if (!pml4e.present)
    return 0;

  // guest PDPT
  auto const pdpt = reinterpret_cast<pdpte_64*>(host_physical_memory_base
    + (pml4e.page_frame_number << 12));
  auto const pdpte = pdpt[vaddr.pdpt_idx];

  if (!pdpte.present)
    return 0;

  if (pdpte.large_page) {
    pdpte_1gb_64 pdpte_1gb;
    pdpte_1gb.flags = pdpte.flags;

    auto const offset = (vaddr.pd_idx << 21) + (vaddr.pt_idx << 12) + vaddr.offset;

    // 1GB
    if (offset_to_next_page)
      *offset_to_next_page = 0x40000000 - offset;

    return (pdpte_1gb.page_frame_number << 30) + offset;
  }

  // guest PD
  auto const pd = reinterpret_cast<pde_64*>(host_physical_memory_base
    + (pdpte.page_frame_number << 12));
  auto const pde = pd[vaddr.pd_idx];

  if (!pde.present)
    return 0;

  if (pde.large_page) {
    pde_2mb_64 pde_2mb;
    pde_2mb.flags = pde.flags;

    auto const offset = (vaddr.pt_idx << 12) + vaddr.offset;

    // 2MB page
    if (offset_to_next_page)
      *offset_to_next_page = 0x200000 - offset;

    return (pde_2mb.page_frame_number << 21) + offset;
  }

  // guest PT
  auto const pt = reinterpret_cast<pte_64*>(host_physical_memory_base
    + (pde.page_frame_number << 12));
  auto const pte = pt[vaddr.pt_idx];

  if (!pte.present)
    return 0;

  // 4KB page
  if (offset_to_next_page)
    *offset_to_next_page = 0x1000 - vaddr.offset;

  return (pte.page_frame_number << 12) + vaddr.offset;
}

// translate a GVA to a GPA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the GPA in order to modify the GVA.
uint64_t gva2gpa(void* const guest_virtual_address, size_t* const offset_to_next_page) {
  cr3 guest_cr3;
  guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);
  return gva2gpa(guest_cr3, guest_virtual_address, offset_to_next_page);
}

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(cr3 const guest_cr3, void* const guest_virtual_address, size_t* const offset_to_next_page) {
  auto const gpa = gva2gpa(guest_cr3, guest_virtual_address, offset_to_next_page);
  if (!gpa)
    return nullptr;
  return host_physical_memory_base + gpa;
}

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(void* const guest_virtual_address, size_t* const offset_to_next_page) {
  cr3 guest_cr3;
  guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);
  return gva2hva(guest_cr3, guest_virtual_address, offset_to_next_page);
}

// attempt to read the memory at the specified guest virtual address from root-mode
size_t read_guest_virtual_memory(cr3 const guest_cr3,
    void* const guest_virtual_address, void* const buffer, size_t const size) {
  // the GVA that we're reading from
  auto const src = reinterpret_cast<uint8_t*>(guest_virtual_address);

  // the HVA that we're writing to
  auto const dst = reinterpret_cast<uint8_t*>(buffer);

  size_t bytes_read = 0;

  // translate and read 1 page at a time
  while (bytes_read < size) {
    size_t src_remaining = 0;

    // translate the guest virtual address to a host virtual address
    auto const curr_src = gva2hva(guest_cr3, src + bytes_read, &src_remaining);

    // paged out
    if (!curr_src)
      return bytes_read;

    // the maximum allowed size that we can read at once with the translated HVA
    auto const curr_size = min(size - bytes_read, src_remaining);

    host_exception_info e;
    memcpy_safe(e, dst + bytes_read, curr_src, curr_size);

    // this shouldn't ever happen...
    if (e.exception_occurred) {
      HV_LOG_ERROR("Failed to memcpy in read_guest_virtual_memory().");
      return bytes_read;
    }

    bytes_read += curr_size;
  }

  return bytes_read;
}

// attempt to read the memory at the specified guest virtual address from root-mode
size_t read_guest_virtual_memory(void* const guest_virtual_address, void* const buffer, size_t const size) {
  cr3 guest_cr3;
  guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);
  return read_guest_virtual_memory(guest_cr3, guest_virtual_address, buffer, size);
}

} // namespace hv
