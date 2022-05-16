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
static size_t read_phys_mem(void* const dst,
    uint64_t const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_read_phys_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = reinterpret_cast<uint64_t>(dst);
  input.args[1] = src;
  input.args[2] = size;
  return hv::vmx_vmcall(input);
}
static size_t write_phys_mem(uint64_t const dst,
    void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_write_phys_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = dst;
  input.args[1] = reinterpret_cast<uint64_t>(src);
  input.args[2] = size;
  return hv::vmx_vmcall(input);
}
static size_t read_virt_mem(cr3 const cr3, void* const dst,
    void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_read_virt_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3.flags;
  input.args[1] = reinterpret_cast<uint64_t>(dst);
  input.args[2] = reinterpret_cast<uint64_t>(src);
  input.args[3] = size;
  return hv::vmx_vmcall(input);
}
static size_t write_virt_mem(cr3 const cr3, void* const dst,
    void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_write_virt_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3.flags;
  input.args[1] = reinterpret_cast<uint64_t>(dst);
  input.args[2] = reinterpret_cast<uint64_t>(src);
  input.args[3] = size;
  return hv::vmx_vmcall(input);
}
static cr3 query_process_cr3(uint64_t const pid) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_query_process_cr3;
  input.key     = hv::hypercall_key;
  input.args[0] = pid;

  cr3 cr3;
  cr3.flags = hv::vmx_vmcall(input);
  return cr3;
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

  int buffer                  = 0;
  int target_int              = 12345;
  auto const target_phys_addr = MmGetPhysicalAddress(&target_int).QuadPart;

  DbgPrint("[client] Reading physical memory at address <0x%zX>.\n", target_phys_addr);

  auto bytes_copied = read_phys_mem(&buffer, target_phys_addr, 4);

  DbgPrint("[client] Read %zu bytes of physical memory.\n", bytes_copied);
  DbgPrint("[client] buffer = %d (should be 12345).\n", buffer);

  DbgPrint("[client] Writing 69 to physical memory at address <0x%zX>.\n", target_phys_addr);

  buffer = 69;
  bytes_copied = write_phys_mem(target_phys_addr, &buffer, 4);

  DbgPrint("[client] Wrote %zu bytes to physical memory.\n", bytes_copied);
  DbgPrint("[client] target_int = %d (should be 69).\n", target_int);

  DbgPrint("[client] Querying System CR3.\n");

  auto const system_cr3 = query_process_cr3(4);

  DbgPrint("[client] system_cr3 = 0x%zX.\n", system_cr3.flags);

  // reset test values
  target_int = 69;
  buffer     = 0;

  DbgPrint("[client] Reading virtual memory at address <0x%p>.\n", &target_int);

  bytes_copied = read_virt_mem(system_cr3, &buffer, &target_int, 4);

  DbgPrint("[client] Read %zu bytes of virtual memory.\n", bytes_copied);
  DbgPrint("[client] buffer = %d (should be 69).\n", buffer);

  DbgPrint("[client] Writing 420 to virtual memory at address <0x%p>.\n", &target_int);

  buffer = 420;
  bytes_copied = write_virt_mem(system_cr3, &target_int, &buffer, 4);

  DbgPrint("[client] Wrote %zu bytes to virtual memory.\n", bytes_copied);
  DbgPrint("[client] target_int = %d (should be 420).\n", target_int);

  return STATUS_SUCCESS;
}

