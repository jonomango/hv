#pragma once

#include <ia32.hpp>

namespace hv {

// virtualize the current system
bool start();

// only one instance of the hypervisor may be running on the system
struct hypervisor {
  // dynamically allocated array of vcpus
  unsigned long vcpu_count = 0;
  class vcpu* vcpus = nullptr;

  // kernel CR3 value of the System process
  cr3 system_cr3;
};

// get the global hypervisor
hypervisor const& ghv();

} // namespace hv
