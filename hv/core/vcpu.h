#pragma once

#include "idt.h"
#include "tss.h"

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

class vcpu {
public:
  // virtualize the current cpu
  // note, this assumes that execution is already restricted to the desired cpu
  bool virtualize();

private:
  // check if VMX operation is supported
  bool check_vmx_capabilities() const;

  // perform certain actions that are required before entering vmx operation
  void enable_vmx_operation();

  // set the working-vmcs pointer to point to our vmcs structure
  bool set_vmcs_pointer();

  // called for every vm-exit
  static void handle_vm_exit(struct guest_context* ctx);

  // called for every host interrupt
  static void handle_host_interrupt(struct trap_frame* frame);

private:
  // functions defined in vmcs.cpp

  // initialize exit, entry, and execution control fields in the vmcs
  void write_ctrl_vmcs_fields();

  // initialize host-state fields in the vmcs
  void write_host_vmcs_fields();

  // initialize guest-state fields in the vmcs
  void write_guest_vmcs_fields();

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

  // host interrupt descriptor table
  alignas(8) interrupt_gate_descriptor_64 host_idt_[host_idt_descriptor_count];

  // host global descriptor table
  alignas(8) segment_descriptor_32 host_gdt_[host_gdt_descriptor_count];
};

} // namespace hv

