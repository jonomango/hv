#pragma once

#include "gdt.h"

#include <ia32.hpp>

namespace hv {

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

  // function that is called on every vm-exit
  static void handle_vm_exit(struct guest_context* ctx);

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
  static constexpr size_t host_stack_size = 0x6000;
  alignas(0x10) uint8_t host_stack_[host_stack_size];

  // host global descriptor table
  gdt gdt_;
};

} // namespace hv

