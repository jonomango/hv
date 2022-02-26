#include "hv.h"
#include "vcpu.h"
#include "mm.h"
#include "arch.h"

namespace hv {

hypervisor ghv;

// allocate and initialize various hypervisor structures before virtualizing
static bool prepare_hv() {
  ghv.vcpu_count = KeQueryActiveProcessorCount(nullptr);

  // size of the vcpu array
  auto const arr_size = sizeof(vcpu) * ghv.vcpu_count;

  // allocate an array of vcpus
  ghv.vcpus = static_cast<vcpu*>(alloc_aligned(arr_size, alignof(vcpu)));

  if (!ghv.vcpus) {
    DbgPrint("[hv] Failed to alloocate VCPUs.\n");
    return false;
  }

  DbgPrint("[hv] Allocated %u VCPUs (0x%zX bytes).\n", ghv.vcpu_count, arr_size);

  // zero-initialize the vcpu array
  memset(ghv.vcpus, 0, arr_size);

  // store the System cr3 value (found in the System EPROCESS structure)
  ghv.system_cr3 = *reinterpret_cast<cr3*>(
    reinterpret_cast<uint8_t*>(PsInitialSystemProcess) + 0x28);

  prepare_host_page_tables();
  
  DbgPrint("[hv] Mapped all of physical memory to address 0x%zX.\n",
    reinterpret_cast<uint64_t>(host_physical_memory_base));

  return true;
}

// virtualize the current system
bool start() {
  if (!prepare_hv())
    return false;

  // we need to be running at an IRQL below DISPATCH_LEVEL so
  // that KeSetSystemAffinityThreadEx takes effect immediately
  NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

  // virtualize every cpu
  for (unsigned long i = 0; i < ghv.vcpu_count; ++i) {
    // restrict execution to the specified cpu
    auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << i);

    if (!virtualize_cpu(&ghv.vcpus[i])) {
      // TODO: handle this bruh -_-
      KeRevertToUserAffinityThreadEx(orig_affinity);
      return false;
    }

    KeRevertToUserAffinityThreadEx(orig_affinity);
  }

  return true;
}

} // namespace hv

