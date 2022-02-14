#include "hv.h"

#include <ntddk.h>

void driver_unload(PDRIVER_OBJECT) {
  DbgPrint("[hv] driver unloaded.\n");
}

NTSTATUS driver_entry(PDRIVER_OBJECT const driver, PUNICODE_STRING) {
  if (driver)
    driver->DriverUnload = driver_unload;

  DbgPrint("[hv] driver loaded.\n");

  if (!hv::start())
    DbgPrint("[hv] failed to virtualize system.\n");

  return STATUS_SUCCESS;
}

