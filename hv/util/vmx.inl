#pragma once

namespace hv {

namespace impl {

// helper function that adjusts vmcs control
// fields according to their capability
inline void write_vmcs_ctrl_field(size_t value,
    unsigned long const ctrl_field,
    unsigned long const cap_msr,
    unsigned long const true_cap_msr) {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // read the "true" capability msr if it is supported
  auto const cap = __readmsr(vmx_basic.vmx_controls ? true_cap_msr : cap_msr);

  // adjust the control according to the capability msr
  value &= cap >> 32;
  value |= cap & 0xFFFFFFFF;

  // write to the vmcs field
  vmx_vmwrite(ctrl_field, value);
}

} // namespace impl

// VMXON instruction
inline bool vmx_vmxon(uint64_t vmxon_phys_addr) {
  return __vmx_on(&vmxon_phys_addr) == 0;
}

// VMXOFF instruction
inline void vmx_vmxoff() {
  __vmx_off();
}

// VMCLEAR instruction
inline bool vmx_vmclear(uint64_t vmcs_phys_addr) {
  return __vmx_vmclear(&vmcs_phys_addr) == 0;
}

// VMPTRLD instruction
inline bool vmx_vmptrld(uint64_t vmcs_phys_addr) {
  return __vmx_vmptrld(&vmcs_phys_addr) == 0;
}

// VMWRITE instruction
inline void vmx_vmwrite(uint64_t const field, uint64_t const value) {
  __vmx_vmwrite(field, value);
}

// VMREAD instruction
inline uint64_t vmx_vmread(uint64_t const field) {
  uint64_t value;
  __vmx_vmread(field, &value);
  return value;
}

// write to the pin-based vm-execution controls
inline void write_ctrl_pin_based(ia32_vmx_pinbased_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PINBASED_CTLS,
    IA32_VMX_TRUE_PINBASED_CTLS);
}

// write to the processor-based vm-execution controls
inline void write_ctrl_proc_based(ia32_vmx_procbased_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PROCBASED_CTLS,
    IA32_VMX_TRUE_PROCBASED_CTLS);
}

// write to the secondary processor-based vm-execution controls
inline void write_ctrl_proc_based2(ia32_vmx_procbased_ctls2_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PROCBASED_CTLS2,
    IA32_VMX_PROCBASED_CTLS2);
}

// write to the vm-exit controls
inline void write_ctrl_exit(ia32_vmx_exit_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_VMEXIT_CONTROLS,
    IA32_VMX_EXIT_CTLS,
    IA32_VMX_TRUE_EXIT_CTLS);
}

// write to the vm-entry controls
inline void write_ctrl_entry(ia32_vmx_entry_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_VMENTRY_CONTROLS,
    IA32_VMX_ENTRY_CTLS,
    IA32_VMX_TRUE_ENTRY_CTLS);
}

// increment the instruction pointer after emulating an instruction
inline void skip_instruction() {
  // increment rip
  vmx_vmwrite(VMCS_GUEST_RIP, vmx_vmread(VMCS_GUEST_RIP)
    + vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH));

  // if we're currently blocking interrupts (due to mov ss or sti)
  // then we should unblock them since we just emulated an instruction
  vmx_interruptibility_state int_state;
  int_state.flags = static_cast<uint32_t>(vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY_STATE));
  int_state.blocking_by_mov_ss = 0;
  int_state.blocking_by_sti    = 0;
  vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, int_state.flags);

  ia32_debugctl_register debugctl;
  debugctl.flags = vmx_vmread(VMCS_GUEST_DEBUGCTL);

  rflags rflags;
  rflags.flags = vmx_vmread(VMCS_GUEST_RFLAGS);

  // if we're single-stepping, inject a debug exception
  // just like normal instruction execution would
  if (rflags.trap_flag && !debugctl.btf) {
    vmx_pending_debug_exceptions dbg_exception;
    dbg_exception.flags = vmx_vmread(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS);
    dbg_exception.bs    = 1;
    vmx_vmwrite(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, dbg_exception.flags);
  }
}

// inject a vectored exception into the guest
inline void inject_hw_exception(uint32_t const vector) {
  vmentry_interrupt_information interrupt_info{};
  interrupt_info.flags              = 0;
  interrupt_info.vector             = vector;
  interrupt_info.interruption_type  = hardware_exception;
  interrupt_info.deliver_error_code = 0;
  interrupt_info.valid              = 1;

  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
}

// inject a vectored exception into the guest (with an error code)
inline void inject_hw_exception(uint32_t const vector, uint32_t const error) {
  vmentry_interrupt_information interrupt_info{};
  interrupt_info.flags              = 0;
  interrupt_info.vector             = vector;
  interrupt_info.interruption_type  = hardware_exception;
  interrupt_info.deliver_error_code = 1;
  interrupt_info.valid              = 1;

  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE, error);
}

} // namespace hv

