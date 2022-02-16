#include "hv.h"

#include <ntddk.h>

void driver_unload(PDRIVER_OBJECT) {
  DbgPrint("[hv] Driver unloaded.\n");
}

NTSTATUS driver_entry(PDRIVER_OBJECT const driver, PUNICODE_STRING) {
  if (driver)
    driver->DriverUnload = driver_unload;

  DbgPrint("[hv] Driver loaded.\n");

  if (!hv::start())
    DbgPrint("[hv] Failed to virtualize system.\n");

  return STATUS_SUCCESS;
}

