#include "vmcs.h"
#include "vcpu.h"

#include "../util/mm.h"
#include "../util/arch.h"
#include "../util/segment.h"

namespace hv {

// defined in vm-exit.asm
extern void __vm_exit();

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
  __vmx_vmwrite(ctrl_field, value);
}

// write to the pin-based vm-execution controls
void write_ctrl_pin_based(ia32_vmx_pinbased_ctls_register const value) {
  write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PINBASED_CTLS,
    IA32_VMX_TRUE_PINBASED_CTLS);
}

// write to the processor-based vm-execution controls
void write_ctrl_proc_based(ia32_vmx_procbased_ctls_register const value) {
  write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PROCBASED_CTLS,
    IA32_VMX_TRUE_PROCBASED_CTLS);
}

// write to the secondary processor-based vm-execution controls
void write_ctrl_proc_based2(ia32_vmx_procbased_ctls2_register const value) {
  write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
    IA32_VMX_PROCBASED_CTLS2,
    IA32_VMX_PROCBASED_CTLS2);
}

// write to the vm-exit controls
void write_ctrl_exit(ia32_vmx_exit_ctls_register const value) {
  write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_VMEXIT_CONTROLS,
    IA32_VMX_EXIT_CTLS,
    IA32_VMX_TRUE_EXIT_CTLS);
}

// write to the vm-entry controls
void write_ctrl_entry(ia32_vmx_entry_ctls_register const value) {
  write_vmcs_ctrl_field(value.flags,
    VMCS_CTRL_VMENTRY_CONTROLS,
    IA32_VMX_ENTRY_CTLS,
    IA32_VMX_TRUE_ENTRY_CTLS);
}

// initialize exit, entry, and execution control fields in the vmcs
void vcpu::write_ctrl_vmcs_fields() {
  // 3.26.2

  // 3.24.6.1
  ia32_vmx_pinbased_ctls_register pin_based_ctrl;
  pin_based_ctrl.flags = 0;
  write_ctrl_pin_based(pin_based_ctrl);

  // 3.24.6.2
  ia32_vmx_procbased_ctls_register proc_based_ctrl;
  proc_based_ctrl.flags                       = 0;
  proc_based_ctrl.cr3_load_exiting            = 1;
  proc_based_ctrl.cr3_store_exiting           = 1;
  proc_based_ctrl.use_msr_bitmaps             = 1;
  proc_based_ctrl.activate_secondary_controls = 1;
  proc_based_ctrl.hlt_exiting = 1;
  write_ctrl_proc_based(proc_based_ctrl);

  // 3.24.6.2
  ia32_vmx_procbased_ctls2_register proc_based_ctrl2;
  proc_based_ctrl2.flags                  = 0;
  proc_based_ctrl2.enable_rdtscp          = 1;
  proc_based_ctrl2.enable_invpcid         = 1;
  proc_based_ctrl2.enable_xsaves          = 1;
  proc_based_ctrl2.enable_user_wait_pause = 1;
  proc_based_ctrl2.conceal_vmx_from_pt    = 1;
  write_ctrl_proc_based2(proc_based_ctrl2);

  // 3.24.7
  ia32_vmx_exit_ctls_register exit_ctrl;
  exit_ctrl.flags                   = 0;
  exit_ctrl.save_debug_controls     = 1;
  exit_ctrl.host_address_space_size = 1;
  exit_ctrl.conceal_vmx_from_pt     = 1;
  write_ctrl_exit(exit_ctrl);

  // 3.24.8
  ia32_vmx_entry_ctls_register entry_ctrl;
  entry_ctrl.flags               = 0;
  entry_ctrl.load_debug_controls = 1;
  entry_ctrl.ia32e_mode_guest    = 1;
  entry_ctrl.conceal_vmx_from_pt = 1;
  write_ctrl_entry(entry_ctrl);

  // 3.24.6.3
  __vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, 0);

  // set up the pagefault mask and match in such a way so
  // that a vm-exit is never triggered for a pagefault
  __vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK,  0);
  __vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, 0);

  // 3.24.6.6
  __vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0);
  __vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW,     0);
  __vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0);
  __vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW,     0);

  // 3.24.6.7
  __vmx_vmwrite(VMCS_CTRL_CR3_TARGET_COUNT,   0);
  __vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_0, 0);
  __vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_1, 0);
  __vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_2, 0);
  __vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_3, 0);

  // 3.24.6.9
  __vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, get_physical(&msr_bitmap_));

  // 3.24.7.2
  __vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT,   0);
  __vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_ADDRESS, 0);
  __vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT,    0);
  __vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_ADDRESS,  0);

  // 3.24.8.2
  __vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT,   0);
  __vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_ADDRESS, 0);

  // 3.24.8.3
  __vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0);
  __vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE,           0);
  __vmx_vmwrite(VMCS_CTRL_VMENTRY_INSTRUCTION_LENGTH,             0);
}

// initialize host-state fields in the vmcs
void vcpu::write_host_vmcs_fields() {
  // 3.24.5
  // 3.26.2

  // TODO: we should be using our own control registers (even for cr0/cr4)
  __vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
  __vmx_vmwrite(VMCS_HOST_CR3, __readcr3());
  __vmx_vmwrite(VMCS_HOST_CR4, __readcr4());

  // ensure that rsp is NOT aligned to 16 bytes when execution starts
  auto const rsp = ((reinterpret_cast<size_t>(
    host_stack_) + host_stack_size) & ~0b1111ull) - 8;

  __vmx_vmwrite(VMCS_HOST_RSP, rsp);
  __vmx_vmwrite(VMCS_HOST_RIP, reinterpret_cast<size_t>(__vm_exit));

  segment_descriptor_register_64 gdtr, idtr;
  _sgdt(&gdtr);
  __sidt(&idtr);

  // TODO: we should be using our own segment selectors (CS, FS, GS, TR)
  __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, 0x10);
  __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0x18);
  __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0x28);
  __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0x28);
  __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0x50);
  __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0x28);
  __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, 0x40);

  // TODO: we should be using our own tss/gdt/idt
  __vmx_vmwrite(VMCS_HOST_FS_BASE,   _readfsbase_u64());
  __vmx_vmwrite(VMCS_HOST_GS_BASE,   _readgsbase_u64());
  __vmx_vmwrite(VMCS_HOST_TR_BASE,   segment_base(gdtr, 0x40));
  __vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtr.base_address);
  __vmx_vmwrite(VMCS_HOST_IDTR_BASE, idtr.base_address);

  // these dont matter to us since the host never executes any syscalls
  // TODO: i believe these can be 0, since the only
  //   requirement is that they contain a conanical address
  __vmx_vmwrite(VMCS_HOST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
  __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
  __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
}

// initialize guest-state fields in the vmcs
void vcpu::write_guest_vmcs_fields() {
  // 3.24.4
  // 3.26.3

  __vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
  __vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());
  __vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());

  __vmx_vmwrite(VMCS_GUEST_DR7, __readdr(7));

  auto const rip = alloc(0x10, NonPagedPoolExecute);
  memset(rip, 0xF4, 0x10);

  // RIP and RSP are set elsewhere
  __vmx_vmwrite(VMCS_GUEST_RSP, 0);
  __vmx_vmwrite(VMCS_GUEST_RIP, reinterpret_cast<size_t>(rip));
  __vmx_vmwrite(VMCS_GUEST_RFLAGS, __readeflags());

  // TODO: don't hardcode the segment selectors idiot...

  __vmx_vmwrite(VMCS_GUEST_CS_SELECTOR,   0x10);
  __vmx_vmwrite(VMCS_GUEST_SS_SELECTOR,   0x18);
  __vmx_vmwrite(VMCS_GUEST_DS_SELECTOR,   0x2B);
  __vmx_vmwrite(VMCS_GUEST_ES_SELECTOR,   0x2B);
  __vmx_vmwrite(VMCS_GUEST_FS_SELECTOR,   0x53);
  __vmx_vmwrite(VMCS_GUEST_GS_SELECTOR,   0x2B);
  __vmx_vmwrite(VMCS_GUEST_TR_SELECTOR,   0x40);
  __vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, 0x00);

  segment_descriptor_register_64 gdtr, idtr;
  _sgdt(&gdtr);
  __sidt(&idtr);

  __vmx_vmwrite(VMCS_GUEST_CS_BASE,   segment_base(gdtr, 0x10));
  __vmx_vmwrite(VMCS_GUEST_SS_BASE,   segment_base(gdtr, 0x18));
  __vmx_vmwrite(VMCS_GUEST_DS_BASE,   segment_base(gdtr, 0x2B));
  __vmx_vmwrite(VMCS_GUEST_ES_BASE,   segment_base(gdtr, 0x2B));
  __vmx_vmwrite(VMCS_GUEST_FS_BASE,   __readmsr(IA32_FS_BASE));
  __vmx_vmwrite(VMCS_GUEST_GS_BASE,   __readmsr(IA32_GS_BASE));
  __vmx_vmwrite(VMCS_GUEST_TR_BASE,   segment_base(gdtr, 0x40));
  __vmx_vmwrite(VMCS_GUEST_LDTR_BASE, segment_base(gdtr, 0x00));

  __vmx_vmwrite(VMCS_GUEST_CS_LIMIT,   __segmentlimit(0x10));
  __vmx_vmwrite(VMCS_GUEST_SS_LIMIT,   __segmentlimit(0x18));
  __vmx_vmwrite(VMCS_GUEST_DS_LIMIT,   __segmentlimit(0x2B));
  __vmx_vmwrite(VMCS_GUEST_ES_LIMIT,   __segmentlimit(0x2B));
  __vmx_vmwrite(VMCS_GUEST_FS_LIMIT,   __segmentlimit(0x53));
  __vmx_vmwrite(VMCS_GUEST_GS_LIMIT,   __segmentlimit(0x2B));
  __vmx_vmwrite(VMCS_GUEST_TR_LIMIT,   __segmentlimit(0x40));
  __vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, __segmentlimit(0x00));

  __vmx_vmwrite(VMCS_GUEST_CS_LIMIT,   segment_access(gdtr, 0x10).flags);
  __vmx_vmwrite(VMCS_GUEST_SS_LIMIT,   segment_access(gdtr, 0x18).flags);
  __vmx_vmwrite(VMCS_GUEST_DS_LIMIT,   segment_access(gdtr, 0x2B).flags);
  __vmx_vmwrite(VMCS_GUEST_ES_LIMIT,   segment_access(gdtr, 0x2B).flags);
  __vmx_vmwrite(VMCS_GUEST_FS_LIMIT,   segment_access(gdtr, 0x53).flags);
  __vmx_vmwrite(VMCS_GUEST_GS_LIMIT,   segment_access(gdtr, 0x2B).flags);
  __vmx_vmwrite(VMCS_GUEST_TR_LIMIT,   segment_access(gdtr, 0x40).flags);
  __vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, segment_access(gdtr, 0x00).flags);

  __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdtr.base_address);
  __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, idtr.base_address);

  __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr.limit);
  __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr.limit);

  __vmx_vmwrite(VMCS_GUEST_DEBUGCTL,     __readmsr(IA32_DEBUGCTL));
  __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
  __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
  __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));

  __vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, vmx_active);

  __vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);

  __vmx_vmwrite(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

  __vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, 0xFFFFFFFF'FFFFFFFFull);
}

} // namespace hv
