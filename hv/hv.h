#pragma once

#include <ia32.hpp>

namespace hv {

struct hypervisor {
  // dynamically allocated array of vcpus
  unsigned long vcpu_count = 0;
  class vcpu* vcpus = nullptr;

  // kernel CR3 value of the System process
  cr3 system_cr3;
};

// global instance of the hypervisor
extern hypervisor ghv;

// virtualize the current system
bool start();

} // namespace hv
