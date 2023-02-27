#pragma once

#include <ia32.hpp>

namespace hv {

struct vcpu;

// try to hide the vm-exit overhead from being detected through timings
void hide_vm_exit_overhead(vcpu* cpu);

// measure the overhead of a vm-exit (RDTSC)
uint64_t measure_vm_exit_tsc_overhead();

// measure the overhead of a vm-exit (CPU_CLK_UNHALTED.REF_TSC)
uint64_t measure_vm_exit_ref_tsc_overhead();

// measure the overhead of a vm-exit (IA32_MPERF)
uint64_t measure_vm_exit_mperf_overhead();

} // namespace hv

