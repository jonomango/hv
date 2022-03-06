#pragma once

#include <ia32.hpp>

namespace hv {

struct vcpu;

// hypercall indices
enum hypercall_code : uint64_t {
  hypercall_ping = 0,
  hypercall_read_virt_mem
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

// read virtual memory from another process
void read_virt_mem(vcpu* cpu);

} // namespace hc

} // namespace hv

