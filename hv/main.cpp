#include "hv.h"

#include <ntddk.h>
#include <ia32.hpp>

// simple hypercall wrappers
static uint64_t ping() {
  hv::hypercall_input input;
  input.code = hv::hypercall_ping;
  input.key  = hv::hypercall_key;
  return hv::vmx_vmcall(input);
}

void driver_unload(PDRIVER_OBJECT) {
  hv::stop();

  DbgPrint("[hv] Devirtualized the system.\n");
  DbgPrint("[hv] Driver unloaded.\n");
}

NTSTATUS driver_entry(PDRIVER_OBJECT const driver, PUNICODE_STRING) {
  DbgPrint("[hv] Driver loaded.\n");

  if (driver)
    driver->DriverUnload = driver_unload;

  if (!hv::start()) {
    DbgPrint("[hv] Failed to virtualize system.\n");
    return STATUS_HV_OPERATION_FAILED;
  }

  if (ping() == hv::hypervisor_signature)
    DbgPrint("[client] Hypervisor signature matches.\n");
  else
    DbgPrint("[client] Failed to ping hypervisor!\n");

  uint32_t count = 32;
  hv::logger_msg msgs[32];
  hv::logger_flush(count, msgs);

  for (uint32_t i = 0; i < count; ++i)
    DbgPrint("[%u] %s\n", msgs[i].id, msgs[i].data);

  return STATUS_SUCCESS;
}

