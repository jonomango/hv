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

  auto const mem = (uint8_t*)ExAllocatePool(NonPagedPool, 0x1000);
  memset(mem, 69, 0x1000);

  DbgPrint("MEM[0]=%X.\n", mem[0]);

  auto const irql = KeRaiseIrqlToDpcLevel();

  hv::hypercall_input input;
  input.key = hv::hypercall_key;
  input.code = hv::hypercall_hide_physical_page;
  input.args[0] = MmGetPhysicalAddress(mem).QuadPart >> 12;
  hv::vmx_vmcall(input);

  DbgPrint("MEM[0]=%X.\n", mem[0]);

  input.key = hv::hypercall_key;
  input.code = hv::hypercall_unhide_physical_page;
  input.args[0] = MmGetPhysicalAddress(mem).QuadPart >> 12;
  hv::vmx_vmcall(input);

  DbgPrint("MEM[0]=%X.\n", mem[0]);

  KeLowerIrql(irql);

  return STATUS_SUCCESS;
}

