#include "vcpu.h"

#include <intrin.h>

namespace hv {

// virtualize the current cpu
// note, this assumes that execution is already restricted to the desired cpu
bool vcpu::virtualize() {
  if (!check_capabilities())
    return false;

  return true;
}

// check if VMX operation is supported
bool vcpu::check_capabilities() const {
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

} // namespace hv
