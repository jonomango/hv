#pragma once

#include "guest-context.h"
#include "page-tables.h"
#include "gdt.h"
#include "idt.h"
#include "ept.h"
#include "vmx.h"
#include "timing.h"

namespace hv {

// size of the host stack for handling vm-exits
inline constexpr size_t host_stack_size = 0x6000;

// guest virtual-processor identifier
inline constexpr uint16_t guest_vpid = 1;

struct vcpu_cached_data {
  // maximum number of bits in a physical address (MAXPHYSADDR)
  uint64_t max_phys_addr;

  // reserved bits in CR0/CR4
  uint64_t vmx_cr0_fixed0;
  uint64_t vmx_cr0_fixed1;
  uint64_t vmx_cr4_fixed0;
  uint64_t vmx_cr4_fixed1;

  // mask of unsupported processor state components for XCR0
  uint64_t xcr0_unsupported_mask;

  // IA32_FEATURE_CONTROL
  ia32_feature_control_register feature_control;
  ia32_feature_control_register guest_feature_control;

  // IA32_VMX_MISC
  ia32_vmx_misc_register vmx_misc;

  // CPUID 0x01
  cpuid_eax_01 cpuid_01;
};

struct vcpu {
  // 4 KiB vmxon region
  alignas(0x1000) vmxon vmxon;

  // 4 KiB vmcs region
  alignas(0x1000) vmcs vmcs;

  // 4 KiB msr bitmap
  alignas(0x1000) vmx_msr_bitmap msr_bitmap;

  // host stack used for handling vm-exits
  alignas(0x1000) uint8_t host_stack[host_stack_size];

  // host interrupt descriptor table
  alignas(0x1000) segment_descriptor_interrupt_gate_64 host_idt[host_idt_descriptor_count];

  // host global descriptor table
  alignas(0x1000) segment_descriptor_32 host_gdt[host_gdt_descriptor_count];

  // host task state segment
  alignas(0x1000) task_state_segment_64 host_tss;

  // EPT paging structures
  alignas(0x1000) vcpu_ept_data ept;

  // vm-exit MSR store area
  struct alignas(0x10) {
    vmx_msr_entry tsc;
    vmx_msr_entry perf_global_ctrl;
    vmx_msr_entry aperf;
    vmx_msr_entry mperf;
  } msr_exit_store;

  // vm-entry MSR load area
  struct alignas(0x10) {
    vmx_msr_entry aperf;
    vmx_msr_entry mperf;
  } msr_entry_load;

  // cached values that are assumed to NEVER change
  vcpu_cached_data cached;

  // pointer to the current guest context, set in exit-handler
  guest_context* ctx;

  // the number of NMIs that need to be delivered
  uint32_t volatile queued_nmis;

  // current TSC offset
  uint64_t tsc_offset;

  // current preemption timer
  uint64_t preemption_timer;

  // the overhead caused by world-transitions
  uint64_t vm_exit_tsc_overhead;
  uint64_t vm_exit_mperf_overhead;
  uint64_t vm_exit_ref_tsc_overhead;

  // whether to use TSC offsetting for the current vm-exit--false by default
  bool hide_vm_exit_overhead;

  // whether to devirtualize the current VCPU
  bool stop_virtualization;
};

// virtualize the specified cpu. this assumes that execution is already
// restricted to the desired logical proocessor.
bool virtualize_cpu(vcpu* cpu);

} // namespace hv

