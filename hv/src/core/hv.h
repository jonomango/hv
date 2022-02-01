#pragma once

namespace hv {

// virtualize the current system
bool start();

// only one instance of the hypervisor may be running on the system
struct hypervisor {
  size_t vcpu_count;
};

// get a pointer to the global hypervisor
hypervisor* ghv();

} // namespace hv
