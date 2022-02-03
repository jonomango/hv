#include "core/hv.h"

#include <ntddk.h>

void driver_unload(PDRIVER_OBJECT) {
  DbgPrint("[hv] driver unloaded.");
}

NTSTATUS driver_entry(PDRIVER_OBJECT const driver, PUNICODE_STRING) {
  DbgPrint("[hv] driver loaded.");

  driver->DriverUnload = driver_unload;

  if (!hv::start())
    DbgPrint("[hv] failed to virtualize system.");

  return STATUS_SUCCESS;
}

