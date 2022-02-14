#pragma once

#include "page-tables.h"

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

class vcpu {
public:
  // virtualize the current cpu
  // note, this assumes that execution is already restricted to the desired cpu
  bool virtualize();

private:
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
  alignas(0x1000) host_page_tables host_page_tables_;

  // host interrupt descriptor table
  segment_descriptor_interrupt_gate_64 host_idt_[host_idt_descriptor_count];

  // host global descriptor table
  segment_descriptor_32 host_gdt_[host_gdt_descriptor_count];

  // host control registers
  cr0 host_cr0_;
  cr3 host_cr3_;
  cr4 host_cr4_;
};

} // namespace hv

