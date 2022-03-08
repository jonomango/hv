#pragma once

#include <ia32.hpp>

namespace hv {

struct vcpu;

// hypercall indices
enum hypercall_code : uint64_t {
  hypercall_ping = 0,
  hypercall_read_virt_mem,
  hypercall_write_virt_mem,
  hypercall_query_process_cr3
};

// hypercall input
struct hypercall_input {
  // rax
  hypercall_code code;

  // rcx, rdx, r8, r9, r10, r11
  uint64_t args[6];
};

namespace hc {

// ping the hypervisor to make sure it is running
void ping(vcpu* cpu);

// read from virtual memory from another process
void read_virt_mem(vcpu* cpu);

// write to virtual memory from another process
void write_virt_mem(vcpu* cpu);

// get the kernel CR3 value of an arbitrary process
void query_process_cr3(vcpu* cpu);

} // namespace hc

} // namespace hv

