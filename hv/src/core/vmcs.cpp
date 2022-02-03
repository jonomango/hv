#include "vmcs.h"

#include "../util/mm.h"
#include "../util/arch.h"

namespace hv {

// prepare the vmcs before launching the virtual machine
bool prepare_vmcs(vmcs& vmcs) {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.2
  vmcs.revision_id = vmx_basic.vmcs_revision_id;
  vmcs.shadow_vmcs_indicator = 0;

  auto vmcs_phys = get_physical(&vmcs);
  NT_ASSERT(vmcs_phys % 0x1000 == 0);

  if (__vmx_vmclear(&vmcs_phys) != 0)
    return false;

  if (__vmx_vmptrld(&vmcs_phys) != 0)
    return false;

  // write revision id
  // msr bitmap
  // initialize host fields
  //   stack
  //   idt
  //   gdt
  //   tss
  //   segment selectors/bases
  // initialize guest fields
  //   clone everyting
  // initialize control fields

  return true;
}

} // namespace hv
