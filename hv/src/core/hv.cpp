#include "hv.h"
#include "vcpu.h"

#include "../util/mm.h"

namespace hv {

static hypervisor* global_hypervisor = nullptr;

// virtualize the current system
bool start() {
  // hypervisor is already running -_-
  if (global_hypervisor)
    return false;

  global_hypervisor = alloc<hypervisor>();
  if (!global_hypervisor)
    return false;

  memset(global_hypervisor, 0, sizeof(hypervisor));

  global_hypervisor->vcpu_count = KeQueryActiveProcessorCount(nullptr);

  // allocate an array of vcpus
  global_hypervisor->vcpus = static_cast<vcpu*>(
    alloc_aligned(sizeof(vcpu) * global_hypervisor->vcpu_count, alignof(vcpu)));

  if (!global_hypervisor->vcpus) {
    free(global_hypervisor);
    return false;
  }

  // virtualize every cpu
  for (unsigned long i = 0; i < global_hypervisor->vcpu_count; ++i) {
    vcpu& cpu = global_hypervisor->vcpus[i];

    // restrict execution to the specified cpu
    auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << i);

    if (!cpu.virtualize()) {
      // TODO: handle this bruh -_-
      return false;
    }

    KeRevertToUserAffinityThreadEx(orig_affinity);
  }

  return true;
}

// temporary (sorta)
void stop() {
  if (!global_hypervisor)
    return;

  free(global_hypervisor->vcpus);
  free(global_hypervisor);

  global_hypervisor = nullptr;
}

// get a pointer to the global hypervisor
hypervisor* ghv() {
  return global_hypervisor;
}

} // namespace hv

