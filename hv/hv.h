#pragma once

#include "page-tables.h"
#include "hypercalls.h"
#include "vmx.h"

#include <ntddk.h>

namespace hv {

struct hypervisor {
  // host page tables that are shared between vcpus
  host_page_tables host_page_tables;

  // dynamically allocated array of vcpus
  unsigned long vcpu_count;
  struct vcpu* vcpus;

  // pointer to the System process
  uint8_t* system_eprocess;

  // kernel CR3 value of the System process
  cr3 system_cr3;

  // windows specific offsets D:
  uint64_t kprocess_directory_table_base_offset;
  uint64_t eprocess_unique_process_id_offset;
};

// global instance of the hypervisor
extern hypervisor ghv;

// virtualize the current system
bool start();

} // namespace hv

