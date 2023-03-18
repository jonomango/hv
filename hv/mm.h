#pragma once

#include <ntddk.h>
#include <ia32.hpp>

namespace hv {

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

// translate a GVA to a GPA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the GPA in order to modify the GVA.
uint64_t gva2gpa(cr3 guest_cr3, void* guest_virtual_address, size_t* offset_to_next_page = nullptr);

// translate a GVA to a GPA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the GPA in order to modify the GVA.
uint64_t gva2gpa(void* guest_virtual_address, size_t* offset_to_next_page = nullptr);

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(cr3 guest_cr3, void* guest_virtual_address, size_t* offset_to_next_page = nullptr);

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(void* guest_virtual_address, size_t* offset_to_next_page = nullptr);

// attempt to read the memory at the specified guest virtual address from root-mode
size_t read_guest_virtual_memory(cr3 guest_cr3, void* guest_virtual_address, void* buffer, size_t size);

// attempt to read the memory at the specified guest virtual address from root-mode
size_t read_guest_virtual_memory(void* guest_virtual_address, void* buffer, size_t size);

} // namespace hv

