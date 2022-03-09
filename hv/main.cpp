#include "hv.h"

#include <ntddk.h>
#include <windef.h>

extern "C" {

NTSYSAPI PVOID RtlPcToFileHeader(
  PVOID PcValue,
  PVOID *BaseOfImage
);

} // extern "C"

size_t get_image_size(void* const image) {
  auto const base = reinterpret_cast<uint8_t*>(image);

  // image NT headers
  auto const nth = base + *reinterpret_cast<uint32_t*>(base + 0x3C);

  return *reinterpret_cast<uint32_t*>(nth + 0x50);
}

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

  void* image_base = nullptr;
  RtlPcToFileHeader(driver_entry, &image_base);

  // hypervisor code must be marked as shared
  hv::mark_shared_memory(image_base, get_image_size(image_base));

  if (!hv::start()) {
    DbgPrint("[hv] Failed to virtualize system.\n");
    return STATUS_HV_OPERATION_FAILED;
  }

  return STATUS_SUCCESS;
}

