#pragma once

namespace hv {

// virtualize the current system
bool start();

// only one instance of the hypervisor may be running on the system
struct hypervisor {
  unsigned long vcpu_count = 0;
  class vcpu* vcpus = nullptr;
};

// get the global hypervisor
hypervisor const& ghv();

} // namespace hv
