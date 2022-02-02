#pragma once

namespace hv {

class vcpu;

// virtualize the current system
bool start();

// temporary (sorta)
void stop();

// only one instance of the hypervisor may be running on the system
struct hypervisor {
  unsigned long vcpu_count;
  vcpu* vcpus;
};

// get a pointer to the global hypervisor
hypervisor* ghv();

} // namespace hv
