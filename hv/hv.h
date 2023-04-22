#pragma once

#include "page-tables.h"
#include "hypercalls.h"
#include "logger.h"
#include "vmx.h"

#include <ntddk.h>

namespace hv {

// signature that is returned by the ping hypercall
inline constexpr uint64_t hypervisor_signature = 'fr0g';

struct hypervisor {
  // host page tables that are shared between vcpus
  host_page_tables host_page_tables;

  // logger that can be used in root-mode
  logger logger;

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
  uint64_t eprocess_image_file_name;
  uint64_t kpcr_pcrb_offset;
  uint64_t kprcb_current_thread_offset;
  uint64_t kthread_apc_state_offset;
  uint64_t kapc_state_process_offset;
};

// global instance of the hypervisor
extern hypervisor ghv;

// virtualize the current system
bool start();

// devirtualize the current system
void stop();

} // namespace hv

