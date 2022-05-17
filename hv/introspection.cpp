#include "introspection.h"
#include "hv.h"

namespace hv {

// TODO: translate using gva2hva instead of directly reading guest memory...

// get the KPCR of the current guest (the pointer should stay constant per-vcpu)
PKPCR current_guest_kpcr() {
  // GS base holds the KPCR when in ring-0
  if (current_guest_cpl() == 0)
    return reinterpret_cast<PKPCR>(vmx_vmread(VMCS_GUEST_GS_BASE));

  // when in ring-3, the GS_SWAP contains the KPCR
  return reinterpret_cast<PKPCR>(__readmsr(IA32_KERNEL_GS_BASE));
}

// get the ETHREAD of the current guest
PETHREAD current_guest_ethread() {
  // KPCR
  auto const kpcr = current_guest_kpcr();

  if (!kpcr)
    return nullptr;

  // KPCR::Prcb
  auto const kprcb = reinterpret_cast<uint8_t*>(kpcr)
    + ghv.kpcr_pcrb_offset;

  // KPCRB::CurrentThread
  return *reinterpret_cast<PETHREAD*>(kprcb
    + ghv.kprcb_current_thread_offset);
}

// get the EPROCESS of the current guest
PEPROCESS current_guest_eprocess() {
  // ETHREAD (KTHREAD is first field as well)
  auto const ethread = current_guest_ethread();

  // KTHREAD::ApcState
  auto const kapc_state = reinterpret_cast<uint8_t*>(ethread)
    + ghv.kthread_apc_state_offset;

  // KAPC_STATE::Process
  return *reinterpret_cast<PEPROCESS*>(kapc_state
    + ghv.kapc_state_process_offset);
}

} // namespace hv

