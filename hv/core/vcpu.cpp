#include "vcpu.h"
#include "vmcs.h"
#include "gdt.h"
#include "exit-handlers.h"

#include "../util/mm.h"
#include "../util/vmx.h"
#include "../util/arch.h"
#include "../util/trap-frame.h"
#include "../util/guest-context.h"

namespace hv {

// defined in vm-launch.asm
extern bool __vm_launch();

// virtualize the current cpu
// note, this assumes that execution is already restricted to the desired cpu
bool vcpu::virtualize() {
  if (!check_vmx_capabilities())
    return false;

  enable_vmx_operation();

  DbgPrint("[hv] enabled vmx operation.\n");

  auto vmxon_phys = get_physical(&vmxon_);
  NT_ASSERT(vmxon_phys % 0x1000 == 0);

  // enter vmx operation
  if (!vmx_vmxon(vmxon_phys)) {
    // TODO: cleanup
    return false;
  }

  // 3.28.3.3.4
  vmx_invept(invept_all_context, {});

  DbgPrint("[hv] entered vmx operation.\n");

  if (!set_vmcs_pointer()) {
    // TODO: cleanup

    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] set vmcs pointer.\n");

  // setup the msr bitmap so that we don't vm-exit on any msr access
  memset(&msr_bitmap_, 0, sizeof(msr_bitmap_));

  // we don't care about anything that is in the TSS
  memset(&host_tss_, 0, sizeof(host_tss_));

  prepare_host_idt(host_idt_);
  prepare_host_gdt(host_gdt_, reinterpret_cast<uint64_t>(&host_tss_));

  DbgPrint("[hv] initialized external host structures.\n");

  // initialize the vmcs fields
  write_ctrl_vmcs_fields();
  write_host_vmcs_fields();
  write_guest_vmcs_fields();

  DbgPrint("[hv] initialized vmcs fields.\n");

  // launch the virtual machine
  if (!__vm_launch()) {
    DbgPrint("[hv] vmlaunch failed, error = %lli.\n", vmx_vmread(VMCS_VM_INSTRUCTION_ERROR));

    // TODO: cleanup

    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] virtualized cpu #%i\n", KeGetCurrentProcessorIndex());

  return true;
}

// check if VMX operation is supported
bool vcpu::check_vmx_capabilities() const {
  cpuid_eax_01 cpuid;
  __cpuid(reinterpret_cast<int*>(&cpuid), 1);

  // 3.23.6
  if (!cpuid.cpuid_feature_information_ecx.virtual_machine_extensions)
    return false;

  ia32_feature_control_register msr;
  msr.flags = __readmsr(IA32_FEATURE_CONTROL);

  // 3.23.7
  if (!msr.lock_bit || !msr.enable_vmx_outside_smx)
    return false;

  return true;
}

// perform certain actions that are required before entering vmx operation
void vcpu::enable_vmx_operation() {
  _disable();

  auto cr0 = __readcr0();
  auto cr4 = __readcr4();

  // 3.23.7
  cr4 |= CR4_VMX_ENABLE_FLAG;

  // 3.23.8
  cr0 |= __readmsr(IA32_VMX_CR0_FIXED0);
  cr0 &= __readmsr(IA32_VMX_CR0_FIXED1);
  cr4 |= __readmsr(IA32_VMX_CR4_FIXED0);
  cr4 &= __readmsr(IA32_VMX_CR4_FIXED1);

  __writecr0(cr0);
  __writecr4(cr4);

  _enable();

  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.11.5
  vmxon_.revision_id = vmx_basic.vmcs_revision_id;
  vmxon_.must_be_zero = 0;
}

// set the working-vmcs pointer to point to our vmcs structure
bool vcpu::set_vmcs_pointer() {
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

// called for every vm-exit
void vcpu::handle_vm_exit(guest_context* const ctx) {
  vmx_vmexit_reason exit_reason;
  exit_reason.flags = static_cast<uint32_t>(vmx_vmread(VMCS_EXIT_REASON));

  switch (exit_reason.basic_exit_reason) {
  case VMX_EXIT_REASON_MOV_CR:
    handle_mov_cr(ctx);
    break;
  case VMX_EXIT_REASON_EXECUTE_CPUID:
    emulate_cpuid(ctx);
    break;
  case VMX_EXIT_REASON_EXECUTE_RDMSR:
    emulate_rdmsr(ctx);
    break;
  case VMX_EXIT_REASON_EXECUTE_WRMSR:
    emulate_wrmsr(ctx);
    break;
  case VMX_EXIT_REASON_EXCEPTION_OR_NMI:
    handle_exception_or_nmi(ctx);
    break;
  case VMX_EXIT_REASON_NMI_WINDOW:
    handle_nmi_window(ctx);
    break;
  default:
    __debugbreak();
    DbgPrint("[hv] vm-exit occurred. RIP=0x%zX.\n", vmx_vmread(VMCS_GUEST_RIP));
    break;
  }
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
