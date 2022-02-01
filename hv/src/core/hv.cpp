#include "hv.h"

#include "../util/mm.h"

namespace hv {

static hypervisor* global_hypervisor_data = nullptr;

// virtualize the current system
bool start() {
  // hypervisor is already running -_-
  if (global_hypervisor_data)
    return false;

  global_hypervisor_data = alloc<hypervisor>();
  if (!global_hypervisor_data)
    return false;

  return true;
}

// get a pointer to the global hypervisor
hypervisor* ghv() {
  return global_hypervisor_data;
}

} // namespace hv
