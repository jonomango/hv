#pragma once

#include <ia32.hpp>

namespace hv {

struct vcpu;

// hypercall indices
enum hypercall : uint64_t {
  hypercall_ping = 0,
  hypercall_read_phys_mem,
  hypercall_write_phys_mem
};

namespace hc {

// ping the hypervisor to make sure it is running
void ping(vcpu* cpu);

// read from arbitrary physical memory
void read_phys_mem(vcpu* cpu);

// write to arbitrary physical memory
void write_phys_mem(vcpu* cpu);

} // namespace hc

} // namespace hv

