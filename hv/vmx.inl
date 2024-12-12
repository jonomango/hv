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

// write to a guest general-purpose register
inline void write_guest_gpr(guest_context* const ctx,
    uint64_t const gpr_idx, uint64_t const value) {
  if (gpr_idx == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    vmx_vmwrite(VMCS_GUEST_RSP, value);
  else
    ctx->gpr[gpr_idx] = value;
}

// read a guest general-purpose register
inline uint64_t read_guest_gpr(guest_context const* const ctx,
    uint64_t const gpr_idx) {
  if (gpr_idx == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    return vmx_vmread(VMCS_GUEST_RSP);
  return ctx->gpr[gpr_idx];
}

// get the value of CR0 that the guest believes is active.
// this is a mixture of the guest CR0 and the CR0 read shadow.
inline cr0 read_effective_guest_cr0() {
  // TODO: cache this value
  auto const mask = vmx_vmread(VMCS_CTRL_CR0_GUEST_HOST_MASK);

  // bits set to 1 in the mask are read from CR0, otherwise from the shadow
  cr0 cr0;
  cr0.flags = (vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW) & mask)
    | (vmx_vmread(VMCS_GUEST_CR0) & ~mask);

  return cr0;
}

// get the value of CR4 that the guest believes is active.
// this is a mixture of the guest CR4 and the CR4 read shadow.
inline cr4 read_effective_guest_cr4() {
  // TODO: cache this value
  auto const mask = vmx_vmread(VMCS_CTRL_CR4_GUEST_HOST_MASK);

  // bits set to 1 in the mask are read from CR4, otherwise from the shadow
  cr4 cr4;
  cr4.flags = (vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW) & mask)
    | (vmx_vmread(VMCS_GUEST_CR4) & ~mask);

  return cr4;
}

// write to the guest interruptibility state
inline void write_interruptibility_state(vmx_interruptibility_state const value) {
  vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, value.flags);
}

// read the guest interruptibility state
inline vmx_interruptibility_state read_interruptibility_state() {
  vmx_interruptibility_state value;
  value.flags = static_cast<uint32_t>(vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY_STATE));
  return value;
}

// write to the pin-based vm-execution controls
inline void write_ctrl_pin_based_safe(ia32_vmx_pinbased_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PINBASED_CTLS,
    IA32_VMX_TRUE_PINBASED_CTLS);
}

// write to the processor-based vm-execution controls
inline void write_ctrl_proc_based_safe(ia32_vmx_procbased_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PROCBASED_CTLS,
    IA32_VMX_TRUE_PROCBASED_CTLS);
}

// write to the secondary processor-based vm-execution controls
inline void write_ctrl_proc_based2_safe(ia32_vmx_procbased_ctls2_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PROCBASED_CTLS2,
    IA32_VMX_PROCBASED_CTLS2);
}

// write to the vm-exit controls
inline void write_ctrl_exit_safe(ia32_vmx_exit_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PRIMARY_VMEXIT_CONTROLS,
    IA32_VMX_EXIT_CTLS,
    IA32_VMX_TRUE_EXIT_CTLS);
}

// write to the vm-entry controls
inline void write_ctrl_entry_safe(ia32_vmx_entry_ctls_register const value) {
  impl::write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_VMENTRY_CONTROLS,
    IA32_VMX_ENTRY_CTLS,
    IA32_VMX_TRUE_ENTRY_CTLS);
}

// write to the pin-based vm-execution controls
inline void write_ctrl_pin_based(ia32_vmx_pinbased_ctls_register const value) {
  vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, value.flags);
}

// write to the processor-based vm-execution controls
inline void write_ctrl_proc_based(ia32_vmx_procbased_ctls_register const value) {
  vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, value.flags);
}

// write to the secondary processor-based vm-execution controls
inline void write_ctrl_proc_based2(ia32_vmx_procbased_ctls2_register const value) {
  vmx_vmwrite(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, value.flags);
}

// write to the vm-exit controls
inline void write_ctrl_exit(ia32_vmx_exit_ctls_register const value) {
  vmx_vmwrite(VMCS_CTRL_PRIMARY_VMEXIT_CONTROLS, value.flags);
}

// write to the vm-entry controls
inline void write_ctrl_entry(ia32_vmx_entry_ctls_register const value) {
  vmx_vmwrite(VMCS_CTRL_VMENTRY_CONTROLS, value.flags);
}

// read the pin-based vm-execution controls
inline ia32_vmx_pinbased_ctls_register read_ctrl_pin_based() {
  ia32_vmx_pinbased_ctls_register value;
  value.flags = vmx_vmread(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS);
  return value;
}

// read the processor-based vm-execution controls
inline ia32_vmx_procbased_ctls_register read_ctrl_proc_based() {
  ia32_vmx_procbased_ctls_register value;
  value.flags = vmx_vmread(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS);
  return value;
}

// read the secondary processor-based vm-execution controls
inline ia32_vmx_procbased_ctls2_register read_ctrl_proc_based2() {
  ia32_vmx_procbased_ctls2_register value;
  value.flags = vmx_vmread(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS);
  return value;
}

// read the vm-exit controls
inline ia32_vmx_exit_ctls_register read_ctrl_exit() {
  ia32_vmx_exit_ctls_register value;
  value.flags = vmx_vmread(VMCS_CTRL_PRIMARY_VMEXIT_CONTROLS);
  return value;
}

// read the vm-entry controls
inline ia32_vmx_entry_ctls_register read_ctrl_entry() {
  ia32_vmx_entry_ctls_register value;
  value.flags = vmx_vmread(VMCS_CTRL_VMENTRY_CONTROLS);
  return value;
}

// get the CPL (current privilege level) of the current guest
inline uint16_t current_guest_cpl() {
  vmx_segment_access_rights ss;
  ss.flags = static_cast<uint32_t>(vmx_vmread(VMCS_GUEST_SS_ACCESS_RIGHTS));
  return ss.descriptor_privilege_level;
}

// increment the instruction pointer after emulating an instruction
inline void skip_instruction() {
  // increment RIP
  auto const old_rip = vmx_vmread(VMCS_GUEST_RIP);
  auto new_rip       = old_rip + vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH);

  // handle wrap-around for 32-bit addresses
  // https://patchwork.kernel.org/project/kvm/patch/20200427165917.31799-1-pbonzini@redhat.com/
  if (old_rip < (1ull << 32) && new_rip >= (1ull << 32)) {
    vmx_segment_access_rights cs_access_rights;
    cs_access_rights.flags = static_cast<uint32_t>(
      vmx_vmread(VMCS_GUEST_CS_ACCESS_RIGHTS));

    // make sure guest is in 32-bit mode
    if (!cs_access_rights.long_mode)
      new_rip &= 0xFFFF'FFFF;
  }

  vmx_vmwrite(VMCS_GUEST_RIP, new_rip);

  // if we're currently blocking interrupts (due to mov ss or sti)
  // then we should unblock them since we just emulated an instruction
  auto interrupt_state = read_interruptibility_state();
  interrupt_state.blocking_by_mov_ss = 0;
  interrupt_state.blocking_by_sti    = 0;
  write_interruptibility_state(interrupt_state);

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

// inject an NMI into the guest
inline void inject_nmi() {
  vmentry_interrupt_information interrupt_info;
  interrupt_info.flags              = 0;
  interrupt_info.vector             = nmi;
  interrupt_info.interruption_type  = non_maskable_interrupt;
  interrupt_info.deliver_error_code = 0;
  interrupt_info.valid              = 1;
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
}

// inject a vectored exception into the guest
inline void inject_hw_exception(uint32_t const vector) {
  vmentry_interrupt_information interrupt_info;
  interrupt_info.flags              = 0;
  interrupt_info.vector             = vector;
  interrupt_info.interruption_type  = hardware_exception;
  interrupt_info.deliver_error_code = 0;
  interrupt_info.valid              = 1;
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
}

// inject a vectored exception into the guest (with an error code)
inline void inject_hw_exception(uint32_t const vector, uint32_t const error) {
  vmentry_interrupt_information interrupt_info;
  interrupt_info.flags              = 0;
  interrupt_info.vector             = vector;
  interrupt_info.interruption_type  = hardware_exception;
  interrupt_info.deliver_error_code = 1;
  interrupt_info.valid              = 1;
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE, error);
}

// enable/disable vm-exits when the guest tries to read the specified MSR
inline void enable_exit_for_msr_read(vmx_msr_bitmap& bitmap,
    uint32_t const msr, bool const enable_exiting) {
  auto const bit = 1 << (msr & 0b0111);

  if (msr <= MSR_ID_LOW_MAX) {
    // update the bit in the low bitmap
    if (enable_exiting)
      bitmap.rdmsr_low[msr / 8] |= bit;
    else
      bitmap.rdmsr_low[msr / 8] &= ~bit;
  }

  else if (msr >= MSR_ID_HIGH_MIN && msr <= MSR_ID_HIGH_MAX) {
    // update the bit in the high bitmap
    if (enable_exiting)
      bitmap.rdmsr_high[(msr - MSR_ID_HIGH_MIN) / 8] |= bit;
    else
      bitmap.rdmsr_high[(msr - MSR_ID_HIGH_MIN) / 8] &= ~bit;
  }
}

// enable/disable vm-exits when the guest tries to write to the specified MSR
inline void enable_exit_for_msr_write(vmx_msr_bitmap& bitmap,
    uint32_t const msr, bool const enable_exiting) {
  auto const bit = 1 << (msr & 0b0111);

  if (msr <= MSR_ID_LOW_MAX) {
    // update the bit in the low bitmap
    if (enable_exiting)
      bitmap.wrmsr_low[msr / 8] |= bit;
    else
      bitmap.wrmsr_low[msr / 8] &= ~bit;
  }

  else if (msr >= MSR_ID_HIGH_MIN && msr <= MSR_ID_HIGH_MAX) {
    // update the bit in the high bitmap
    if (enable_exiting)
      bitmap.wrmsr_high[(msr - MSR_ID_HIGH_MIN) / 8] |= bit;
    else
      bitmap.wrmsr_high[(msr - MSR_ID_HIGH_MIN) / 8] &= ~bit;
  }
}

// enable MTF exiting
inline void enable_monitor_trap_flag() {
  auto control = read_ctrl_proc_based();
  control.monitor_trap_flag = 1;
  write_ctrl_proc_based(control);
}

// disable MTF exiting
inline void disable_monitor_trap_flag() {
  auto control = read_ctrl_proc_based();
  control.monitor_trap_flag = 0;
  write_ctrl_proc_based(control);
}

} // namespace hv

