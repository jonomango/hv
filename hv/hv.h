#pragma once

#include "page-tables.h"

#include <ia32.hpp>

namespace hv {

struct hypervisor {
  // host page tables that are shared between vcpus
  host_page_tables host_page_tables;

  // dynamically allocated array of vcpus
  unsigned long vcpu_count;
  struct vcpu* vcpus;

  // kernel CR3 value of the System process
  cr3 system_cr3;
};

// global instance of the hypervisor
extern hypervisor ghv;

// virtualize the current system
bool start();

} // namespace hv
