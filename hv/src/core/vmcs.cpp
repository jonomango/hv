#include "vmcs.h"
#include "vcpu.h"

#include "../util/mm.h"
#include "../util/arch.h"

namespace hv {

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

// initialize exit, entry, and execution control fields in the vmcs
void vcpu::write_ctrl_vmcs_fields() {
  // 3.26.2
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
  __vmx_vmwrite(VMCS_HOST_RIP, 0);

  // TODO: we should be using our own segment selectors (CS, FS, GS, TR)
  __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, 0);
  __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0);
  __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0);
  __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0);
  __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0);
  __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0);
  __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, 0);

  // TODO: we should be using our own tss/gdt/idt
  __vmx_vmwrite(VMCS_HOST_FS_BASE,   _readfsbase_u64());
  __vmx_vmwrite(VMCS_HOST_GS_BASE,   _readgsbase_u64());
  __vmx_vmwrite(VMCS_HOST_TR_BASE,   0);
  __vmx_vmwrite(VMCS_HOST_GDTR_BASE, 0);
  __vmx_vmwrite(VMCS_HOST_IDTR_BASE, 0);

  // these dont matter to us since the host never executes any syscalls
  // TODO: i believe these can be 0, since the only
  //   requirement is that they contain a conanical address
  __vmx_vmwrite(VMCS_HOST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
  __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
  __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
}

} // namespace hv
