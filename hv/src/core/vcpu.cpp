#include "vcpu.h"
#include "vmcs.h"
#include "guest-context.h"

#include "../util/mm.h"
#include "../util/arch.h"

namespace hv {

// virtualize the current cpu
// note, this assumes that execution is already restricted to the desired cpu
bool vcpu::virtualize() {
  if (!check_vmx_capabilities())
    return false;

  enable_vmx_operation();

  auto vmxon_phys = get_physical(&vmxon_);
  NT_ASSERT(vmxon_phys % 0x1000 == 0);

  // enter vmx operation
  if (__vmx_on(&vmxon_phys) != 0) {
    // TODO: cleanup
    return false;
  }

  // 3.28.3.3.4
  __vmx_invept(invept_all_context, {});

  DbgPrint("[hv] entered vmx operation.");

  if (!set_vmcs_pointer()) {
    // TODO: cleanup

    __vmx_off();
    return false;
  }

  DbgPrint("[hv] set vmcs pointer.");

  // we dont want to break on any msr access
  memset(&msr_bitmap_, 0, sizeof(msr_bitmap_));

  // initialize the vmcs fields
  write_ctrl_vmcs_fields();
  write_host_vmcs_fields();
  write_guest_vmcs_fields();

  DbgPrint("[hv] initialized vmcs fields.");

  // launch the virtual machine
  if (auto const status = __vmx_vmlaunch(); status != 0) {
    if (status == 1)
      DbgPrint("[hv] vmlaunch failed, error = %lli.", __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR));
    else
      DbgPrint("[hv] vmlaunch failed.");

    // TODO: cleanup

    __vmx_off();
    return false;
  }

  __vmx_off();

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

  if (__vmx_vmclear(&vmcs_phys) != 0)
    return false;

  if (__vmx_vmptrld(&vmcs_phys) != 0)
    return false;

  return true;
}

// function that is called on every vm-exit
void vcpu::handle_vm_exit(struct guest_context* ctx) {
  DbgPrint("[hv] Hello world! ctx=%p\n", ctx);
}

} // namespace hv
