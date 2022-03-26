#include "hv.h"

#include <ntddk.h>
#include <ia32.hpp>

void driver_unload(PDRIVER_OBJECT) {
  DbgPrint("[hv] Driver unloaded.\n");
}

NTSTATUS driver_entry(PDRIVER_OBJECT const driver, PUNICODE_STRING) {
  if (driver)
    driver->DriverUnload = driver_unload;

  DbgPrint("[hv] Driver loaded.\n");

  if (!hv::create()) {
    DbgPrint("[hv] Failed to create hypervisor.\n");
    return STATUS_HV_OPERATION_FAILED;
  }

  if (!hv::start()) {
    DbgPrint("[hv] Failed to virtualize system.\n");
    return STATUS_HV_OPERATION_FAILED;
  }

  return STATUS_SUCCESS;
}

