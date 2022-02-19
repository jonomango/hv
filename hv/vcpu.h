#pragma once

#include "page-tables.h"
#include "guest-context.h"

#include <ia32.hpp>

namespace hv {

// selectors for the host GDT
inline constexpr segment_selector host_cs_selector = { 0, 0, 1 };
inline constexpr segment_selector host_tr_selector = { 0, 0, 2 };

// number of available descriptor slots in the host GDT
inline constexpr size_t host_gdt_descriptor_count = 4;

// number of available descriptor slots in the host IDT
inline constexpr size_t host_idt_descriptor_count = 256;

// size of the host stack for handling vm-exits
inline constexpr size_t host_stack_size = 0x6000;

// guest virtual-processor identifier
inline constexpr uint16_t guest_vpid = 1;

struct cached_vcpu_data {
  // maximum number of bits in a physical address (MAXPHYSADDR)
  uint64_t max_phys_addr;

  // reserved bits in CR0/CR4
  uint64_t vmx_cr0_fixed0;
  uint64_t vmx_cr0_fixed1;
  uint64_t vmx_cr4_fixed0;
  uint64_t vmx_cr4_fixed1;

  // mask of unsupported processor state components for XCR0
  uint64_t xcr0_unsupported_mask;
};

class vcpu {
public:
  // virtualize the current cpu
  // note, this assumes that execution is already restricted to the desired cpu
  bool virtualize();

  // toggle vm-exiting for this MSR in the MSR bitmap
  void toggle_exiting_for_msr(uint32_t msr, bool enabled);

  // get the current guest context
  guest_context* ctx() const { return guest_ctx_; }

  // get data that is cached per-vcpu
  cached_vcpu_data const* cdata() const { return &cached_; }

private:
  // cache certain values that will be used during vmx operation
  void cache_vcpu_data();

  // perform certain actions that are required before entering vmx operation
  bool enable_vmx_operation();

  // enter vmx operation by executing VMXON
  bool enter_vmx_operation();

  // set the working-vmcs pointer to point to our vmcs structure
  bool load_vmcs_pointer();

  // initialize external structures
  void prepare_external_structures();

  // write VMCS control fields
  void write_vmcs_ctrl_fields();
  
  // write VMCS host fields
  void write_vmcs_host_fields();

  // write VMCS guest fields
  void write_vmcs_guest_fields();

private:
  // called for every vm-exit
  static void handle_vm_exit(struct guest_context* ctx);

  // called for every host interrupt
  static void handle_host_interrupt(struct trap_frame* frame);

private:
  // 4 KiB vmxon region
  alignas(0x1000) vmxon vmxon_;

  // 4 KiB vmcs region
  alignas(0x1000) vmcs vmcs_;

  // 4 KiB msr bitmap
  alignas(0x1000) vmx_msr_bitmap msr_bitmap_;

  // host stack used for handling vm-exits
  alignas(0x1000) uint8_t host_stack_[host_stack_size];

  // host task state segment
  alignas(0x1000) task_state_segment_64 host_tss_;

  // host page tables
  // TODO: make per-hv not per-vcpu
  alignas(0x1000) host_page_tables host_page_tables_;

  // host interrupt descriptor table
  segment_descriptor_interrupt_gate_64 host_idt_[host_idt_descriptor_count];

  // host global descriptor table
  segment_descriptor_32 host_gdt_[host_gdt_descriptor_count];

  // host control registers
  cr0 host_cr0_;
  cr3 host_cr3_;
  cr4 host_cr4_;

  // pointer to the current guest context, set in exit-handler
  guest_context* guest_ctx_;

  // cached values that are assumed to NEVER change
  cached_vcpu_data cached_;
};

} // namespace hv

