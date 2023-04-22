#pragma once

#include <cstdint>
#include <Windows.h>

namespace hv {

// key used for executing hypercalls
inline constexpr uint64_t hypercall_key = 69420;

// signature that is returned by the ping hypercall
inline constexpr uint64_t hypervisor_signature = 'fr0g';

struct logger_msg {
  static constexpr uint32_t max_msg_length = 128;

  // ID of the current message
  uint64_t id;

  // timestamp counter of the current message
  uint64_t tsc;

  // process ID of the VCPU that sent the message
  uint32_t aux;

  // null-terminated ascii string
  char data[max_msg_length];
};

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
  hypercall_get_hv_base
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

// call fn() on each logical processor
template <typename Fn>
void for_each_cpu(Fn fn);

// ping the hypervisor to make sure it is running (returns hypervisor_signature)
uint64_t ping();

// a hypercall for quick testing
uint64_t test(uint64_t a1 = 0, uint64_t a2 = 0,
              uint64_t a3 = 0, uint64_t a4 = 0,
              uint64_t a5 = 0, uint64_t a6 = 0);

// read from arbitrary physical memory
size_t read_phys_mem(void* dst, uint64_t src, size_t size);

// write to arbitrary physical memory
size_t write_phys_mem(uint64_t dst, void const* src, size_t size);

// read from virtual memory in another process
size_t read_virt_mem(uint64_t cr3, void* dst, void const* src, size_t size);

// write to virtual memory in another process
size_t write_virt_mem(uint64_t cr3, void* dst, void const* src, size_t size);

// get the kernel CR3 value of an arbitrary process
uint64_t query_process_cr3(uint64_t pid);

// install an EPT hook for the CURRENT logical processor ONLY
bool install_ept_hook(uint64_t orig_page_pfn, uint64_t exec_page_pfn);

// remove a previously installed EPT hook
void remove_ept_hook(uint64_t orig_page_pfn);

// flush the hypervisor logs into a buffer
void flush_logs(uint32_t& count, logger_msg* msgs);

// translate a virtual address to its physical address
uint64_t get_physical_address(uint64_t cr3, void const* address);

// hide a physical page from the guest
bool hide_physical_page(uint64_t pfn);

// unhide a physical page from the guest
void unhide_physical_page(uint64_t pfn);

// get the base address of the hypervisor
void* get_hv_base();

// VMCALL instruction, defined in hv.asm
uint64_t vmx_vmcall(hypercall_input& input);

/**
* 
* implementation:
* 
**/

// call fn() on each logical processor
template <typename Fn>
inline void for_each_cpu(Fn const fn) {
  SYSTEM_INFO info = {};
  GetSystemInfo(&info);

  for (unsigned int i = 0; i < info.dwNumberOfProcessors; ++i) {
    auto const prev_affinity = SetThreadAffinityMask(GetCurrentThread(), 1ull << i);
    fn();
    SetThreadAffinityMask(GetCurrentThread(), prev_affinity);
  }
}

// ping the hypervisor to make sure it is running (returns hypervisor_signature)
inline uint64_t ping() {
  hv::hypercall_input input;
  input.code = hv::hypercall_ping;
  input.key  = hv::hypercall_key;
  return hv::vmx_vmcall(input);
}

// a hypercall for quick testing
inline uint64_t test(uint64_t const a1, uint64_t const a2,
                     uint64_t const a3, uint64_t const a4,
                     uint64_t const a5, uint64_t const a6) {
  hv::hypercall_input input;
  input.code = hv::hypercall_test;
  input.key  = hv::hypercall_key;
  input.args[0] = a1;
  input.args[1] = a2;
  input.args[2] = a3;
  input.args[3] = a4;
  input.args[4] = a5;
  input.args[5] = a6;
  return hv::vmx_vmcall(input);
}

// read from arbitrary physical memory
inline size_t read_phys_mem(void* const dst, uint64_t const src,
                            size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_read_phys_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = reinterpret_cast<uint64_t>(dst);
  input.args[1] = src;
  input.args[2] = size;
  return hv::vmx_vmcall(input);
}

// write to arbitrary physical memory
inline size_t write_phys_mem(uint64_t const dst, void const* const src,
                             size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_write_phys_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = dst;
  input.args[1] = reinterpret_cast<uint64_t>(src);
  input.args[2] = size;
  return hv::vmx_vmcall(input);
}

// read from virtual memory in another process
inline size_t read_virt_mem(uint64_t const cr3, void* const dst,
                            void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_read_virt_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3;
  input.args[1] = reinterpret_cast<uint64_t>(dst);
  input.args[2] = reinterpret_cast<uint64_t>(src);
  input.args[3] = size;
  return hv::vmx_vmcall(input);
}

// write to virtual memory in another process
inline size_t write_virt_mem(uint64_t const cr3, void* const dst,
                             void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_write_virt_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3;
  input.args[1] = reinterpret_cast<uint64_t>(dst);
  input.args[2] = reinterpret_cast<uint64_t>(src);
  input.args[3] = size;
  return hv::vmx_vmcall(input);
}

// get the kernel CR3 value of an arbitrary process
inline uint64_t query_process_cr3(uint64_t const pid) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_query_process_cr3;
  input.key     = hv::hypercall_key;
  input.args[0] = pid;
  return hv::vmx_vmcall(input);
}

// install an EPT hook for the CURRENT logical processor ONLY
inline bool install_ept_hook(uint64_t const orig_page_pfn, uint64_t const exec_page_pfn) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_install_ept_hook;
  input.key     = hv::hypercall_key;
  input.args[0] = orig_page_pfn;
  input.args[1] = exec_page_pfn;
  return hv::vmx_vmcall(input);
}

// remove a previously installed EPT hook
inline void remove_ept_hook(uint64_t const orig_page_pfn) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_remove_ept_hook;
  input.key     = hv::hypercall_key;
  input.args[0] = orig_page_pfn;
  hv::vmx_vmcall(input);
}

// flush the hypervisor logs into a buffer
inline void flush_logs(uint32_t& count, logger_msg* const msgs) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_flush_logs;
  input.key     = hv::hypercall_key;
  input.args[0] = count;
  input.args[1] = reinterpret_cast<uint64_t>(msgs);
  count = static_cast<uint32_t>(hv::vmx_vmcall(input));
}

// translate a virtual address to its physical address
inline uint64_t get_physical_address(uint64_t const cr3, void const* const address) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_get_physical_address;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3;
  input.args[1] = reinterpret_cast<uint64_t>(address);
  return hv::vmx_vmcall(input);
}

// hide a physical page from the guest
inline bool hide_physical_page(uint64_t const pfn) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_hide_physical_page;
  input.key     = hv::hypercall_key;
  input.args[0] = pfn;
  return hv::vmx_vmcall(input);
}

// unhide a physical page from the guest
inline void unhide_physical_page(uint64_t const pfn) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_unhide_physical_page;
  input.key     = hv::hypercall_key;
  input.args[0] = pfn;
  hv::vmx_vmcall(input);
}

// get the base address of the hypervisor
inline void* get_hv_base() {
  hv::hypercall_input input;
  input.code = hv::hypercall_get_hv_base;
  input.key  = hv::hypercall_key;
  return reinterpret_cast<void*>(hv::vmx_vmcall(input));
}

} // namespace hv

