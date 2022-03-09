#pragma once

#include "page-tables.h"
#include "hypercalls.h"
#include "vmx.h"

#include <ntddk.h>

namespace hv {

struct shared_memory_region {
  static constexpr size_t max_region_count = 16;

  // number of shared regions between the guest and the host
  size_t count;

  struct {
    // base address of the region (GVA and HVA)
    uint8_t* base;

    // size of the region, in bytes
    size_t size;
  } regions[max_region_count];
};

struct hypervisor {
  // host page tables that are shared between vcpus
  host_page_tables host_page_tables;

  // memory that is shared between the host and the guest
  shared_memory_region shared_memory_region;

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

// create the hypervisor
bool create();

// virtualize the current system
bool start();

// mark a region of kernel memory as shared (present in both host and guest)
bool mark_shared_memory(void* start, size_t size);

} // namespace hv
