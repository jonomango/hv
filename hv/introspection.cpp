#include "introspection.h"
#include "mm.h"
#include "hv.h"

namespace hv {

// get the KPCR of the current guest (this pointer should stay constant per-vcpu)
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
  PETHREAD current_thread = nullptr;
  read_guest_virtual_memory(ghv.system_cr3,
    kprcb + ghv.kprcb_current_thread_offset, &current_thread, sizeof(current_thread));

  return current_thread;
}

// get the EPROCESS of the current guest
PEPROCESS current_guest_eprocess() {
  // ETHREAD (KTHREAD is first field as well)
  auto const ethread = current_guest_ethread();

  if (!ethread)
    return nullptr;

  // KTHREAD::ApcState
  auto const kapc_state = reinterpret_cast<uint8_t*>(ethread)
    + ghv.kthread_apc_state_offset;

  // KAPC_STATE::Process
  PEPROCESS process = nullptr;
  read_guest_virtual_memory(ghv.system_cr3,
    kapc_state + ghv.kapc_state_process_offset, &process, sizeof(process));

  return process;
}

// get the PID of the current guest
uint64_t current_guest_pid() {
  // EPROCESS
  auto const process = reinterpret_cast<uint8_t*>(current_guest_eprocess());
  if (!process)
    return 0;

  // EPROCESS::UniqueProcessId
  uint64_t pid = 0;
  read_guest_virtual_memory(ghv.system_cr3,
    process + ghv.eprocess_unique_process_id_offset, &pid, sizeof(pid));

  return pid;
}

// get the kernel CR3 of the current guest
cr3 current_guest_cr3() {
  cr3 cr3;
  cr3.flags = 0;

  // EPROCESS
  auto const process = reinterpret_cast<uint8_t*>(current_guest_eprocess());
  if (!process)
    return cr3;

  // EPROCESS::DirectoryTableBase
  read_guest_virtual_memory(ghv.system_cr3,
    process + ghv.kprocess_directory_table_base_offset, &cr3, sizeof(cr3));

  return cr3;
}

// get the image file name (up to 15 chars) of the current guest process
bool current_guest_image_file_name(char (&name)[16]) {
  memset(name, 0, sizeof(name));

  // EPROCESS
  auto const process = reinterpret_cast<uint8_t*>(current_guest_eprocess());
  if (!process)
    return false;

  // EPROCESS::ImageFileName
  return 15 == read_guest_virtual_memory(ghv.system_cr3,
    process + ghv.eprocess_image_file_name, name, 15);
}

} // namespace hv

