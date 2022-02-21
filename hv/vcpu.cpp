#include "vcpu.h"
#include "hv.h"
#include "gdt.h"
#include "idt.h"
#include "mm.h"
#include "vmx.h"
#include "arch.h"
#include "segment.h"
#include "trap-frame.h"
#include "exit-handlers.h"

namespace hv {

// virtualize the current cpu
// note, this assumes that execution is already restricted to the desired cpu
bool vcpu::virtualize() {
  cache_vcpu_data();

  DbgPrint("[hv] Cached VCPU data.\n");

  if (!enable_vmx_operation())
    return false;

  DbgPrint("[hv] Enabled VMX operation.\n");

  if (!enter_vmx_operation())
    return false;

  DbgPrint("[hv] Entered VMX operation.\n");

  if (!load_vmcs_pointer()) {
    // TODO: cleanup
    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] Set VMCS pointer.\n");

  prepare_external_structures();

  DbgPrint("[hv] Initialized external structures.\n");

  write_vmcs_ctrl_fields();
  write_vmcs_host_fields();
  write_vmcs_guest_fields();

  DbgPrint("[hv] Initialized VMCS fields.\n");

  // launch the virtual machine
  if (!vm_launch()) {
    DbgPrint("[hv] VMLAUNCH failed, error = %lli.\n", vmx_vmread(VMCS_VM_INSTRUCTION_ERROR));

    // TODO: cleanup
    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] Launched VCPU #%i.\n", KeGetCurrentProcessorIndex());

  return true;
}

// toggle vm-exiting for this MSR in the MSR bitmap
void vcpu::toggle_exiting_for_msr(uint32_t msr, bool const enabled) {
  auto const bit = static_cast<uint8_t>(enabled ? 1 : 0);

  if (msr <= MSR_ID_LOW_MAX) {
    // set the bit in the low bitmap
    msr_bitmap_.rdmsr_low[msr / 8] = (bit << (msr & 0b0111));
    msr_bitmap_.wrmsr_low[msr / 8] = (bit << (msr & 0b0111));
  } else if (msr >= MSR_ID_HIGH_MIN && msr <= MSR_ID_HIGH_MAX) {
    msr -= MSR_ID_HIGH_MIN;

    // set the bit in the high bitmap
    msr_bitmap_.rdmsr_high[msr / 8] = (bit << (msr & 0b0111));
    msr_bitmap_.wrmsr_high[msr / 8] = (bit << (msr & 0b0111));
  }
}

// cache certain values that will be used during vmx operation
void vcpu::cache_vcpu_data() {
  cpuid_eax_80000008 cpuid_80000008;
  __cpuid(reinterpret_cast<int*>(&cpuid_80000008), 0x80000008);

  cached_.max_phys_addr = cpuid_80000008.eax.number_of_physical_address_bits;

  cached_.vmx_cr0_fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
  cached_.vmx_cr0_fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);
  cached_.vmx_cr4_fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
  cached_.vmx_cr4_fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);

  cpuid_eax_0d_ecx_00 cpuid_0d;
  __cpuidex(reinterpret_cast<int*>(&cpuid_0d), 0x0D, 0x00);
  
  // features in XCR0 that are supported
  cached_.xcr0_unsupported_mask = ~((static_cast<uint64_t>(
    cpuid_0d.edx.flags) << 32) | cpuid_0d.eax.flags);

  cached_.feature_control.flags = __readmsr(IA32_FEATURE_CONTROL);

  __cpuid(reinterpret_cast<int*>(&cached_.cpuid_01), 0x01);
}

// perform certain actions that are required before entering vmx operation
bool vcpu::enable_vmx_operation() {
  cpuid_eax_01 cpuid_1;
  __cpuid(reinterpret_cast<int*>(&cpuid_1), 1);

  // 3.23.6
  if (!cpuid_1.cpuid_feature_information_ecx.virtual_machine_extensions)
    return false;

  auto const feature_control = cached_.feature_control;

  // 3.23.7
  if (!feature_control.lock_bit || !feature_control.enable_vmx_outside_smx)
    return false;

  _disable();

  auto cr0 = __readcr0();
  auto cr4 = __readcr4();

  // 3.23.7
  cr4 |= CR4_VMX_ENABLE_FLAG;

  // 3.23.8
  cr0 |= cached_.vmx_cr0_fixed0;
  cr0 &= cached_.vmx_cr0_fixed1;
  cr4 |= cached_.vmx_cr4_fixed0;
  cr4 &= cached_.vmx_cr4_fixed1;

  __writecr0(cr0);
  __writecr4(cr4);

  _enable();

  return true;
}

// enter vmx operation by executing VMXON
bool vcpu::enter_vmx_operation() {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.11.5
  vmxon_.revision_id = vmx_basic.vmcs_revision_id;
  vmxon_.must_be_zero = 0;

  auto vmxon_phys = get_physical(&vmxon_);
  NT_ASSERT(vmxon_phys % 0x1000 == 0);

  // enter vmx operation
  if (!vmx_vmxon(vmxon_phys))
    return false;

  // 3.28.3.3.4
  vmx_invept(invept_all_context, {});

  return true;
}

// set the working-vmcs pointer to point to our vmcs structure
bool vcpu::load_vmcs_pointer() {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.2
  vmcs_.revision_id = vmx_basic.vmcs_revision_id;
  vmcs_.shadow_vmcs_indicator = 0;

  auto vmcs_phys = get_physical(&vmcs_);
  NT_ASSERT(vmcs_phys % 0x1000 == 0);

  if (!vmx_vmclear(vmcs_phys))
    return false;

  if (!vmx_vmptrld(vmcs_phys))
    return false;

  return true;
}

// initialize external structures
void vcpu::prepare_external_structures() {
  // setup the MSR bitmap so that we only exit on IA32_FEATURE_CONTROL
  memset(&msr_bitmap_, 0, sizeof(msr_bitmap_));
  toggle_exiting_for_msr(IA32_FEATURE_CONTROL, true);

  // we don't care about anything that's in the TSS
  memset(&host_tss_, 0, sizeof(host_tss_));

  prepare_host_idt(host_idt_);
  prepare_host_gdt(host_gdt_, &host_tss_);
}

// write VMCS control fields
void vcpu::write_vmcs_ctrl_fields() {
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
  proc_based_ctrl2.enable_vpid            = 1;
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

  // set up the mask and match in such a way so
  // that a vm-exit is never triggered for a pagefault
  vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK,  0);
  vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, 0);

  // 3.24.6.6
#ifdef NDEBUG
  // only vm-exit when guest tries to change a reserved bit
  vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK,
    cached_.vmx_cr0_fixed0 | ~cached_.vmx_cr0_fixed1);
  vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK,
    cached_.vmx_cr4_fixed0 | ~cached_.vmx_cr4_fixed1);
#else
  // vm-exit on every CR0/CR4 modification
  vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0xFFFFFFFF'FFFFFFFF);
  vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0xFFFFFFFF'FFFFFFFF);
#endif
  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, __readcr0());
  vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, __readcr4() & ~CR4_VMX_ENABLE_FLAG);

  // 3.24.6.7
  // try to trigger the least amount of CR3 exits as possible
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_COUNT,   1);
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_0, ghv.system_cr3.flags);

  // 3.24.6.9
  vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, get_physical(&msr_bitmap_));

  // 3.24.6.12
  vmx_vmwrite(VMCS_CTRL_VIRTUAL_PROCESSOR_IDENTIFIER, guest_vpid);

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

// write VMCS host fields
void vcpu::write_vmcs_host_fields() {
  // 3.24.5
  // 3.26.2

  cr3 host_cr3;
  host_cr3.flags                     = 0;
  host_cr3.page_level_cache_disable  = 0;
  host_cr3.page_level_write_through  = 0;
  host_cr3.address_of_page_directory = get_physical(&ghv.host_page_tables.pml4) >> 12;
  vmx_vmwrite(VMCS_HOST_CR3, host_cr3.flags);

  // TODO: setup our own CR0/CR4
  vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
  vmx_vmwrite(VMCS_HOST_CR4, __readcr4());

  // ensure that rsp is NOT aligned to 16 bytes when execution starts
  auto const rsp = ((reinterpret_cast<size_t>(
    host_stack_) + host_stack_size) & ~0b1111ull) - 8;

  vmx_vmwrite(VMCS_HOST_RSP, rsp);
  vmx_vmwrite(VMCS_HOST_RIP, reinterpret_cast<size_t>(vm_exit));

  vmx_vmwrite(VMCS_HOST_CS_SELECTOR, host_cs_selector.flags);
  vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_TR_SELECTOR, host_tr_selector.flags);

  vmx_vmwrite(VMCS_HOST_FS_BASE,   reinterpret_cast<size_t>(this));
  vmx_vmwrite(VMCS_HOST_GS_BASE,   0);
  vmx_vmwrite(VMCS_HOST_TR_BASE,   reinterpret_cast<size_t>(&host_tss_));
  vmx_vmwrite(VMCS_HOST_GDTR_BASE, reinterpret_cast<size_t>(&host_gdt_));
  vmx_vmwrite(VMCS_HOST_IDTR_BASE, reinterpret_cast<size_t>(&host_idt_));

  vmx_vmwrite(VMCS_HOST_SYSENTER_CS,  0);
  vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, 0);
  vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, 0);
}

// write VMCS guest fields
void vcpu::write_vmcs_guest_fields() {
  // 3.24.4
  // 3.26.3

  vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());

  vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
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

// called for every vm-exit
void vcpu::handle_vm_exit(guest_context* const ctx) {
  // get the current vcpu
  auto const cpu = reinterpret_cast<vcpu*>(_readfsbase_u64());
  cpu->guest_ctx_ = ctx;

  vmx_vmexit_reason exit_reason;
  exit_reason.flags = static_cast<uint32_t>(vmx_vmread(VMCS_EXIT_REASON));

  vmentry_interrupt_information vectoring_info;
  vectoring_info.flags = static_cast<uint32_t>(vmx_vmread(VMCS_IDT_VECTORING_INFORMATION));

  // vm-exit during event delivery
  // 3.27.2.4
  if (vectoring_info.valid) {
    // an example scenario:
    // + host injects an interrupt into the guest.
    // + the guest IDT is marked as not readable with EPT.
    // + EPT violation (vm-exit) occurs while injecting event (previous interrupt).
    //
    // to properly handle this, we should:
    // + handle the EPT violation.
    // + attempt to inject the previous event again.
    //
    // possible issues:
    // + if the vm-exit handler tries to inject an event.
    //   - we can't inject 2 events at once, so one will have to be lost.
    // + if the vm-exit handler advances RIP then the event will be delivered
    //   on the wrong instruction boundary.
    //   - we should only re-inject for vm-exits that don't emulate instructions.
  }

  switch (exit_reason.basic_exit_reason) {
  case VMX_EXIT_REASON_EXCEPTION_OR_NMI: handle_exception_or_nmi(cpu); break;
  case VMX_EXIT_REASON_EXECUTE_GETSEC:   emulate_getsec(cpu);          break;
  case VMX_EXIT_REASON_EXECUTE_INVD:     emulate_invd(cpu);            break;
  case VMX_EXIT_REASON_NMI_WINDOW:       handle_nmi_window(cpu);       break;
  case VMX_EXIT_REASON_EXECUTE_CPUID:    emulate_cpuid(cpu);           break;
  case VMX_EXIT_REASON_MOV_CR:           handle_mov_cr(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_RDMSR:    emulate_rdmsr(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_WRMSR:    emulate_wrmsr(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_XSETBV:   emulate_xsetbv(cpu);          break;
  case VMX_EXIT_REASON_EXECUTE_VMXON:    emulate_vmxon(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_VMCALL:   handle_vmcall(cpu);           break;

  // VMX instructions (besides VMXON and VMCALL)
  case VMX_EXIT_REASON_EXECUTE_INVEPT:
  case VMX_EXIT_REASON_EXECUTE_INVVPID:
  case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
  case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
  case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
  case VMX_EXIT_REASON_EXECUTE_VMPTRST:
  case VMX_EXIT_REASON_EXECUTE_VMREAD:
  case VMX_EXIT_REASON_EXECUTE_VMRESUME:
  case VMX_EXIT_REASON_EXECUTE_VMWRITE:
  case VMX_EXIT_REASON_EXECUTE_VMXOFF:
  case VMX_EXIT_REASON_EXECUTE_VMFUNC:   handle_vmx_instruction(cpu);  break;
  }

  // guest_ctx_ != nullptr only when it is usable
  cpu->guest_ctx_ = nullptr;
}

// called for every host interrupt
void vcpu::handle_host_interrupt(trap_frame* const frame) {
  switch (frame->vector) {
  // host NMIs
  case nmi:
    auto ctrl = read_ctrl_proc_based();
    ctrl.nmi_window_exiting = 1;
    write_ctrl_proc_based(ctrl);
    break;
  }
}

} // namespace hv
