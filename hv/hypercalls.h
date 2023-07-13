#pragma once

#include <ia32.hpp>

namespace hv {

struct vcpu;

// key used for executing hypercalls
// TODO: compute this at runtime
inline constexpr uint64_t hypercall_key = 69420;

// hypercall indices
enum hypercall_code : uint64_t {
  hypercall_ping = 0,
  hypercall_test,
  hypercall_unload,
  hypercall_read_phys_mem,
  hypercall_write_phys_mem,
  hypercall_read_virt_mem,
  hypercall_write_virt_mem,
  hypercall_query_process_cr3,
  hypercall_install_ept_hook,
  hypercall_remove_ept_hook,
  hypercall_flush_logs,
  hypercall_get_physical_address,
  hypercall_hide_physical_page,
  hypercall_unhide_physical_page,
  hypercall_get_hv_base,
  hypercall_install_mmr,
  hypercall_remove_mmr,
  hypercall_remove_all_mmrs
};

// hypercall input
struct hypercall_input {
  // rax
  struct {
    hypercall_code code : 8;
    uint64_t       key  : 56;
  };

  // rcx, rdx, r8, r9, r10, r11
  uint64_t args[6];
};

namespace hc {

// ping the hypervisor to make sure it is running
void ping(vcpu* cpu);

// a hypercall for quick testing
void test(vcpu* cpu);

// devirtualize the current VCPU
void unload(vcpu* cpu);

// read from arbitrary physical memory
void read_phys_mem(vcpu* cpu);

// write to arbitrary physical memory
void write_phys_mem(vcpu* cpu);

// read from virtual memory in another process
void read_virt_mem(vcpu* cpu);

// write to virtual memory in another process
void write_virt_mem(vcpu* cpu);

// get the kernel CR3 value of an arbitrary process
void query_process_cr3(vcpu* cpu);

// install an EPT hook for the CURRENT logical processor ONLY
void install_ept_hook(vcpu* cpu);

// remove a previously installed EPT hook
void remove_ept_hook(vcpu* cpu);

// flush the hypervisor logs into a buffer
void flush_logs(vcpu* cpu);

// translate a virtual address to its physical address
void get_physical_address(vcpu* cpu);

// hide a physical page from the guest
void hide_physical_page(vcpu* cpu);

// unhide a physical page from the guest
void unhide_physical_page(vcpu* cpu);

// get the base address of the hypervisor
void get_hv_base(vcpu* cpu);

// write to the logger whenever a certain physical memory range is accessed
void install_mmr(vcpu* cpu);

// remove a monitored memory range
void remove_mmr(vcpu* cpu);

// remove every installed MMR
void remove_all_mmrs(vcpu* cpu);

} // namespace hc

} // namespace hv

