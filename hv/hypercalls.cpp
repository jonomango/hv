#include "hypercalls.h"
#include "vcpu.h"
#include "vmx.h"
#include "mm.h"
#include "exception-routines.h"

namespace hv::hc {

// GVA -> HVA
// TODO: account for large pages
// TODO: account for page boundaries (could be done by
//       mapping guest pages into HV address-space?)
static void* translate_guest_address(void* const address, size_t* const size = nullptr) {
  cr3 guest_cr3;
  guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);

  // 4-level virtual address
  union virtual_address {
    void const* address;
    struct {
      uint64_t offset   : 12;
      uint64_t pt_idx   : 9;
      uint64_t pd_idx   : 9;
      uint64_t pdpt_idx : 9;
      uint64_t pml4_idx : 9;
    };
  };

  virtual_address const vaddr = { address };

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

  // guest PD
  auto const pd = reinterpret_cast<pde_64*>(host_physical_memory_base
    + (pdpte.page_frame_number << 12));
  auto const pde = pd[vaddr.pd_idx];

  if (!pde.present)
    return nullptr;

  // guest PT
  auto const pt = reinterpret_cast<pte_64*>(host_physical_memory_base
    + (pde.page_frame_number << 12));
  auto const pte = pt[vaddr.pt_idx];

  if (!pte.present)
    return nullptr;

  if (size)
    *size = 0x1000 - vaddr.offset;

  return host_physical_memory_base + (pte.page_frame_number << 12) + vaddr.offset;
}

// ping the hypervisor to make sure it is running
void ping(vcpu* const cpu) {
  cpu->ctx->rax = hypervisor_signature;

  // we want to hide our vm-exit latency since the hypervisor uses this
  // hypercall for measuring vm-exit latency and we want to follow the
  // same code path as close as possible.
  cpu->hide_vm_exit_latency = true;

  skip_instruction();
}

// read from arbitrary physical memory
void read_phys_mem(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // virtual address
  auto const dst  = reinterpret_cast<uint8_t*>(ctx->rdx);

  // virtual address
  auto const src  = host_physical_memory_base + ctx->r8;

  // size in bytes
  auto const size = ctx->r9;

  for (uint64_t bytes_read = 0; bytes_read < size;) {
    size_t offset_to_next_page = 0;

    // translate the guest virtual address to a host virtual address
    auto const curr_addr = translate_guest_address(
      dst + bytes_read, &offset_to_next_page);

    // dont wanna read past buffer end :)
    auto const curr_size = min(offset_to_next_page, size - bytes_read);

    if (!curr_addr) {
      // TODO: should be a #PF instead
      inject_hw_exception(general_protection, 0);
      return;
    }

    host_exception_info e;
    memcpy_safe(e, curr_addr, src + bytes_read, curr_size);

    // better safe than sorry
    if (e.exception_occurred) {
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  skip_instruction();
}

// write to arbitrary physical memory
void write_phys_mem(vcpu* const) {
  inject_hw_exception(invalid_opcode);
}


} // namespace hv::hc

