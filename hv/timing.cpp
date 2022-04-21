#include "timing.h"
#include "vmx.h"

namespace hv {

// measure the overhead of a vm-exit (RDTSC)
uint64_t measure_vm_exit_tsc_overhead() {
  _disable();

  hypercall_input hv_input;
  hv_input.code = hypercall_ping;
  hv_input.key  = hypercall_key;

  uint64_t lowest = ~0uLL;

  // perform the measurement 10 times and use the smallest time
  for (int i = 0; i < 10; ++i) {
    _mm_lfence();
    auto start = __rdtsc();
    _mm_lfence();

    _mm_lfence();
    auto end = __rdtsc();
    _mm_lfence();

    auto const timing_overhead = (end - start);

    _mm_lfence();
    start = __rdtsc();
    _mm_lfence();

    vmx_vmcall(hv_input);

    _mm_lfence();
    end = __rdtsc();
    _mm_lfence();

    auto const vm_exit_overhead = (end - start);
    auto const adjusted = (vm_exit_overhead - timing_overhead);

    if (adjusted < lowest)
      lowest = adjusted;
  }

  _enable();
  return lowest;
}

// measure the overhead of a vm-exit (CPU_CLK_UNHALTED.REF_TSC)
uint64_t measure_vm_exit_ref_tsc_overhead() {
  // TODO: actually measure this...
  return 300;
}

// measure the overhead of a vm-exit (IA32_MPERF)
uint64_t measure_vm_exit_mperf_overhead() {
  _disable();

  hypercall_input hv_input;
  hv_input.code = hypercall_ping;
  hv_input.key  = hypercall_key;

  uint64_t lowest = ~0uLL;

  // perform the measurement 10 times and use the smallest time
  for (int i = 0; i < 10; ++i) {
    _mm_lfence();
    auto start = __readmsr(IA32_MPERF);
    _mm_lfence();

    _mm_lfence();
    auto end = __readmsr(IA32_MPERF);
    _mm_lfence();

    auto const timing_overhead = (end - start);

    _mm_lfence();
    start = __readmsr(IA32_MPERF);
    _mm_lfence();

    vmx_vmcall(hv_input);

    _mm_lfence();
    end = __readmsr(IA32_MPERF);
    _mm_lfence();

    auto const vm_exit_overhead = (end - start);
    auto const adjusted = (vm_exit_overhead - timing_overhead);

    if (adjusted < lowest)
      lowest = adjusted;
  }

  _enable();
  return lowest;
}

} // namespace hv

