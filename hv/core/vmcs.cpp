#include "vmcs.h"
#include "vcpu.h"

#include "../util/mm.h"
#include "../util/vmx.h"
#include "../util/segment.h"

namespace hv {

// defined in vm-exit.asm
extern void __vm_exit();

// initialize exit, entry, and execution control fields in the vmcs
void vcpu::write_ctrl_vmcs_fields() {
  // 3.26.2

  // 3.24.6.1
  ia32_vmx_pinbased_ctls_register pin_based_ctrl;
  pin_based_ctrl.flags       = 0;
  pin_based_ctrl.virtual_nmi = 1;
  pin_based_ctrl.nmi_exiting = 1;
  write_ctrl_pin_based_safe(pin_based_ctrl);

  // 3.24.6.2
  ia32_vmx_procbased_ctls_register proc_based_ctrl;
  proc_based_ctrl.flags                       = 0;
#ifndef NDEBUG
  proc_based_ctrl.cr3_load_exiting            = 1;
  proc_based_ctrl.cr3_store_exiting           = 1;
#endif
  proc_based_ctrl.use_msr_bitmaps             = 1;
  proc_based_ctrl.activate_secondary_controls = 1;
  write_ctrl_proc_based_safe(proc_based_ctrl);

  // 3.24.6.2
  ia32_vmx_procbased_ctls2_register proc_based_ctrl2;
  proc_based_ctrl2.flags                  = 0;
  proc_based_ctrl2.enable_rdtscp          = 1;
  proc_based_ctrl2.enable_invpcid         = 1;
  proc_based_ctrl2.enable_xsaves          = 1;
  proc_based_ctrl2.enable_user_wait_pause = 1;
  proc_based_ctrl2.conceal_vmx_from_pt    = 1;
  write_ctrl_proc_based2_safe(proc_based_ctrl2);

  // 3.24.7
  ia32_vmx_exit_ctls_register exit_ctrl;
  exit_ctrl.flags                   = 0;
  exit_ctrl.save_debug_controls     = 1;
  exit_ctrl.host_address_space_size = 1;
  exit_ctrl.conceal_vmx_from_pt     = 1;
  write_ctrl_exit_safe(exit_ctrl);

  // 3.24.8
  ia32_vmx_entry_ctls_register entry_ctrl;
  entry_ctrl.flags               = 0;
  entry_ctrl.load_debug_controls = 1;
  entry_ctrl.ia32e_mode_guest    = 1;
  entry_ctrl.conceal_vmx_from_pt = 1;
  write_ctrl_entry_safe(entry_ctrl);

  // 3.24.6.3
  vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, 0);

  // set up the pagefault mask and match in such a way so
  // that a vm-exit is never triggered for a pagefault
  vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK,  0);
  vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, 0);

  // 3.24.6.6
  vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0);
  vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW,     0);
  vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0);
  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW,     0);

  // 3.24.6.7
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_COUNT,   0);
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_0, 0);
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_1, 0);
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_2, 0);
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_3, 0);

  // 3.24.6.9
  vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, get_physical(&msr_bitmap_));

  // 3.24.7.2
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT,   0);
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_ADDRESS, 0);
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT,    0);
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_ADDRESS,  0);

  // 3.24.8.2
  vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT,   0);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_ADDRESS, 0);

  // 3.24.8.3
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE,           0);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INSTRUCTION_LENGTH,             0);
}

// initialize host-state fields in the vmcs
void vcpu::write_host_vmcs_fields() {
  // 3.24.5
  // 3.26.2

  // TODO: we should be using our own control registers (even for cr0/cr4)
  vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
  vmx_vmwrite(VMCS_HOST_CR3, __readcr3());
  vmx_vmwrite(VMCS_HOST_CR4, __readcr4());

  // ensure that rsp is NOT aligned to 16 bytes when execution starts
  auto const rsp = ((reinterpret_cast<size_t>(
    host_stack_) + host_stack_size) & ~0b1111ull) - 8;

  vmx_vmwrite(VMCS_HOST_RSP, rsp);
  vmx_vmwrite(VMCS_HOST_RIP, reinterpret_cast<size_t>(__vm_exit));

  segment_descriptor_register_64 gdtr, idtr;
  _sgdt(&gdtr);
  __sidt(&idtr);

  // TODO: we should be using our own segment selectors (CS, FS, GS, TR)
  vmx_vmwrite(VMCS_HOST_CS_SELECTOR, 0x10);
  //vmx_vmwrite(VMCS_HOST_CS_SELECTOR, host_cs_selector.flags);
  vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0x00);
  //vmx_vmwrite(VMCS_HOST_TR_SELECTOR, host_tr_selector.flags);
  vmx_vmwrite(VMCS_HOST_TR_SELECTOR, 0x40);

  // TODO: we should be using our own tss/gdt/idt
  vmx_vmwrite(VMCS_HOST_FS_BASE,   _readfsbase_u64());
  vmx_vmwrite(VMCS_HOST_GS_BASE,   _readgsbase_u64());
  vmx_vmwrite(VMCS_HOST_TR_BASE,   segment_base(gdtr, 0x40));
  //vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtr.base_address);
  vmx_vmwrite(VMCS_HOST_GDTR_BASE, reinterpret_cast<size_t>(&host_gdt_.descriptors));
  //vmx_vmwrite(VMCS_HOST_IDTR_BASE, idtr.base_address);
  vmx_vmwrite(VMCS_HOST_IDTR_BASE, reinterpret_cast<size_t>(&host_idt_.descriptors));

  // these dont matter to us since the host never executes any syscalls
  // TODO: i believe these can be 0, since the only
  //   requirement is that they contain a conanical address
  vmx_vmwrite(VMCS_HOST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
  vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
  vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
}

// initialize guest-state fields in the vmcs
void vcpu::write_guest_vmcs_fields() {
  // 3.24.4
  // 3.26.3

  vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
  vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());
  vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());

  vmx_vmwrite(VMCS_GUEST_DR7, __readdr(7));

  // RIP and RSP are set in vm-launch.asm
  vmx_vmwrite(VMCS_GUEST_RSP, 0);
  vmx_vmwrite(VMCS_GUEST_RIP, 0);
  vmx_vmwrite(VMCS_GUEST_RFLAGS, __readeflags());

  // TODO: don't hardcode the segment selectors idiot...

  vmx_vmwrite(VMCS_GUEST_CS_SELECTOR,   0x10);
  vmx_vmwrite(VMCS_GUEST_SS_SELECTOR,   0x18);
  vmx_vmwrite(VMCS_GUEST_DS_SELECTOR,   0x2B);
  vmx_vmwrite(VMCS_GUEST_ES_SELECTOR,   0x2B);
  vmx_vmwrite(VMCS_GUEST_FS_SELECTOR,   0x53);
  vmx_vmwrite(VMCS_GUEST_GS_SELECTOR,   0x2B);
  vmx_vmwrite(VMCS_GUEST_TR_SELECTOR,   0x40);
  vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, 0x00);

  segment_descriptor_register_64 gdtr, idtr;
  _sgdt(&gdtr);
  __sidt(&idtr);

  vmx_vmwrite(VMCS_GUEST_CS_BASE,   segment_base(gdtr, 0x10));
  vmx_vmwrite(VMCS_GUEST_SS_BASE,   segment_base(gdtr, 0x18));
  vmx_vmwrite(VMCS_GUEST_DS_BASE,   segment_base(gdtr, 0x2B));
  vmx_vmwrite(VMCS_GUEST_ES_BASE,   segment_base(gdtr, 0x2B));
  vmx_vmwrite(VMCS_GUEST_FS_BASE,   __readmsr(IA32_FS_BASE));
  vmx_vmwrite(VMCS_GUEST_GS_BASE,   __readmsr(IA32_GS_BASE));
  vmx_vmwrite(VMCS_GUEST_TR_BASE,   segment_base(gdtr, 0x40));
  vmx_vmwrite(VMCS_GUEST_LDTR_BASE, segment_base(gdtr, 0x00));

  vmx_vmwrite(VMCS_GUEST_CS_LIMIT,   __segmentlimit(0x10));
  vmx_vmwrite(VMCS_GUEST_SS_LIMIT,   __segmentlimit(0x18));
  vmx_vmwrite(VMCS_GUEST_DS_LIMIT,   __segmentlimit(0x2B));
  vmx_vmwrite(VMCS_GUEST_ES_LIMIT,   __segmentlimit(0x2B));
  vmx_vmwrite(VMCS_GUEST_FS_LIMIT,   __segmentlimit(0x53));
  vmx_vmwrite(VMCS_GUEST_GS_LIMIT,   __segmentlimit(0x2B));
  vmx_vmwrite(VMCS_GUEST_TR_LIMIT,   __segmentlimit(0x40));
  vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, __segmentlimit(0x00));

  vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS,   segment_access(gdtr, 0x10).flags);
  vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS,   segment_access(gdtr, 0x18).flags);
  vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS,   segment_access(gdtr, 0x2B).flags);
  vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS,   segment_access(gdtr, 0x2B).flags);
  vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS,   segment_access(gdtr, 0x53).flags);
  vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS,   segment_access(gdtr, 0x2B).flags);
  vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS,   segment_access(gdtr, 0x40).flags);
  vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, segment_access(gdtr, 0x00).flags);

  vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdtr.base_address);
  vmx_vmwrite(VMCS_GUEST_IDTR_BASE, idtr.base_address);

  vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr.limit);
  vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr.limit);

  vmx_vmwrite(VMCS_GUEST_DEBUGCTL,     __readmsr(IA32_DEBUGCTL));
  vmx_vmwrite(VMCS_GUEST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
  vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
  vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));

  vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, vmx_active);

  vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);

  vmx_vmwrite(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

  vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, 0xFFFFFFFF'FFFFFFFFull);
}

} // namespace hv
