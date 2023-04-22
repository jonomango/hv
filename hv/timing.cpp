#include "timing.h"
#include "vcpu.h"
#include "vmx.h"
#include "logger.h"

#include <ntdef.h>

namespace hv {

// try to hide the vm-exit overhead from being detected through timings
void hide_vm_exit_overhead(vcpu* const cpu) {
  //
  // Guest APERF/MPERF values are stored/restored on vm-entry and vm-exit,
  // however, there appears to be a small, yet constant, overhead that occurs
  // when the CPU is performing these stores and loads. This is the case for
  // every MSR, so naturally PERF_GLOBAL_CTRL is affected as well. If it wasn't
  // for this, hiding vm-exit overhead would be sooooo much easier and cleaner,
  // but whatever.
  //

  ia32_perf_global_ctrl_register perf_global_ctrl;
  perf_global_ctrl.flags = cpu->msr_exit_store.perf_global_ctrl.msr_data;

  // make sure the CPU loads the previously stored guest state on vm-entry
  cpu->msr_entry_load.aperf.msr_data = cpu->msr_exit_store.aperf.msr_data;
  cpu->msr_entry_load.mperf.msr_data = cpu->msr_exit_store.mperf.msr_data;
  vmx_vmwrite(VMCS_GUEST_PERF_GLOBAL_CTRL, perf_global_ctrl.flags);

  // account for the constant overhead associated with loading/storing MSRs
  cpu->msr_entry_load.aperf.msr_data -= cpu->vm_exit_mperf_overhead;
  cpu->msr_entry_load.mperf.msr_data -= cpu->vm_exit_mperf_overhead;

  // account for the constant overhead associated with loading/storing MSRs
  if (perf_global_ctrl.en_fixed_ctrn & (1ull << 2)) {
    auto const cpl = current_guest_cpl();

    ia32_fixed_ctr_ctrl_register fixed_ctr_ctrl;
    fixed_ctr_ctrl.flags = __readmsr(IA32_FIXED_CTR_CTRL);

    // this also needs to be done for many other PMCs, but whatever
    if ((cpl == 0 && fixed_ctr_ctrl.en2_os) || (cpl == 3 && fixed_ctr_ctrl.en2_usr))
      __writemsr(IA32_FIXED_CTR2, __readmsr(IA32_FIXED_CTR2) - cpu->vm_exit_ref_tsc_overhead);
  }  
  
  // this usually occurs for vm-exits that are unlikely to be reliably timed,
  // such as when an exception occurs or if the preemption timer fired
  if (!cpu->hide_vm_exit_overhead || cpu->vm_exit_tsc_overhead > 10000) {
    // this is our chance to resync the TSC
    cpu->tsc_offset = 0;

    // soft disable the VMX preemption timer
    cpu->preemption_timer = ~0ull;

    return;
  }

  // set the preemption timer to cause an exit after 10000 guest TSC ticks have passed
  cpu->preemption_timer = max(2,
    10000 >> cpu->cached.vmx_misc.preemption_timer_tsc_relationship);

  // use TSC offsetting to hide from timing attacks that use the TSC
  cpu->tsc_offset -= cpu->vm_exit_tsc_overhead;
}

// measure the overhead of a vm-exit (RDTSC)
uint64_t measure_vm_exit_tsc_overhead() {
  _disable();

  hypercall_input hv_input;
  hv_input.code = hypercall_ping;
  hv_input.key  = hypercall_key;

  uint64_t lowest_vm_exit_overhead = ~0ull;
  uint64_t lowest_timing_overhead  = ~0ull;

  // perform the measurement 10 times and use the smallest time
  for (int i = 0; i < 10; ++i) {
    _mm_lfence();
    auto start = __rdtsc();
    _mm_lfence();

    _mm_lfence();
    auto end = __rdtsc();
    _mm_lfence();

    auto const timing_overhead = (end - start);

    vmx_vmcall(hv_input);

    _mm_lfence();
    start = __rdtsc();
    _mm_lfence();

    vmx_vmcall(hv_input);

    _mm_lfence();
    end = __rdtsc();
    _mm_lfence();

    auto const vm_exit_overhead = (end - start);

    if (vm_exit_overhead < lowest_vm_exit_overhead)
      lowest_vm_exit_overhead = vm_exit_overhead;
    if (timing_overhead < lowest_timing_overhead)
      lowest_timing_overhead = timing_overhead;
  }

  _enable();
  return lowest_vm_exit_overhead - lowest_timing_overhead;
}

// measure the overhead of a vm-exit (CPU_CLK_UNHALTED.REF_TSC)
uint64_t measure_vm_exit_ref_tsc_overhead() {
  _disable();

  hypercall_input hv_input;
  hv_input.code = hypercall_ping;
  hv_input.key  = hypercall_key;

  ia32_fixed_ctr_ctrl_register curr_fixed_ctr_ctrl;
  curr_fixed_ctr_ctrl.flags = __readmsr(IA32_FIXED_CTR_CTRL);

  ia32_perf_global_ctrl_register curr_perf_global_ctrl;
  curr_perf_global_ctrl.flags = __readmsr(IA32_PERF_GLOBAL_CTRL);

  // enable fixed counter #2
  auto new_fixed_ctr_ctrl = curr_fixed_ctr_ctrl;
  new_fixed_ctr_ctrl.en2_os      = 1;
  new_fixed_ctr_ctrl.en2_usr     = 0;
  new_fixed_ctr_ctrl.en2_pmi     = 0;
  new_fixed_ctr_ctrl.any_thread2 = 0;
  __writemsr(IA32_FIXED_CTR_CTRL, new_fixed_ctr_ctrl.flags);

  // enable fixed counter #2
  auto new_perf_global_ctrl = curr_perf_global_ctrl;
  new_perf_global_ctrl.en_fixed_ctrn |= (1ull << 2);
  __writemsr(IA32_PERF_GLOBAL_CTRL, new_perf_global_ctrl.flags);

  uint64_t lowest_vm_exit_overhead = ~0ull;
  uint64_t lowest_timing_overhead  = ~0ull;

  // perform the measurement 10 times and use the smallest time
  for (int i = 0; i < 10; ++i) {
    _mm_lfence();
    auto start = __readmsr(IA32_FIXED_CTR2);
    _mm_lfence();

    _mm_lfence();
    auto end = __readmsr(IA32_FIXED_CTR2);
    _mm_lfence();

    auto const timing_overhead = (end - start);

    vmx_vmcall(hv_input);

    _mm_lfence();
    start = __readmsr(IA32_FIXED_CTR2);
    _mm_lfence();

    vmx_vmcall(hv_input);

    _mm_lfence();
    end = __readmsr(IA32_FIXED_CTR2);
    _mm_lfence();

    auto const vm_exit_overhead = (end - start);

    if (vm_exit_overhead < lowest_vm_exit_overhead)
      lowest_vm_exit_overhead = vm_exit_overhead;
    if (timing_overhead < lowest_timing_overhead)
      lowest_timing_overhead = timing_overhead;
  }

  // restore MSRs
  __writemsr(IA32_PERF_GLOBAL_CTRL, curr_perf_global_ctrl.flags);
  __writemsr(IA32_FIXED_CTR_CTRL, curr_fixed_ctr_ctrl.flags);

  _enable();
  return lowest_vm_exit_overhead - lowest_timing_overhead;
}

// measure the overhead of a vm-exit (IA32_MPERF)
uint64_t measure_vm_exit_mperf_overhead() {
  _disable();

  hypercall_input hv_input;
  hv_input.code = hypercall_ping;
  hv_input.key  = hypercall_key;

  uint64_t lowest_vm_exit_overhead = ~0ull;
  uint64_t lowest_timing_overhead  = ~0ull;

  // perform the measurement 10 times and use the smallest time
  for (int i = 0; i < 10; ++i) {
    _mm_lfence();
    auto start = __readmsr(IA32_MPERF);
    _mm_lfence();

    _mm_lfence();
    auto end = __readmsr(IA32_MPERF);
    _mm_lfence();

    auto const timing_overhead = (end - start);

    vmx_vmcall(hv_input);

    _mm_lfence();
    start = __readmsr(IA32_MPERF);
    _mm_lfence();

    vmx_vmcall(hv_input);

    _mm_lfence();
    end = __readmsr(IA32_MPERF);
    _mm_lfence();

    auto const vm_exit_overhead = (end - start);

    if (vm_exit_overhead < lowest_vm_exit_overhead)
      lowest_vm_exit_overhead = vm_exit_overhead;
    if (timing_overhead < lowest_timing_overhead)
      lowest_timing_overhead = timing_overhead;
  }

  _enable();
  return lowest_vm_exit_overhead - lowest_timing_overhead;
}

} // namespace hv

