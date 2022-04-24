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
  hypercall_read_phys_mem,
  hypercall_write_phys_mem,
  hypercall_read_virt_mem,
  hypercall_write_virt_mem,
  hypercall_query_process_cr3
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

} // namespace hc

} // namespace hv

