#include "hv.h"
#include "vcpu.h"
#include "mm.h"
#include "arch.h"

namespace hv {

hypervisor ghv;

// create the hypervisor
bool create() {
  ghv.vcpu_count = KeQueryActiveProcessorCount(nullptr);

  // size of the vcpu array
  auto const arr_size = sizeof(vcpu) * ghv.vcpu_count;

  // allocate an array of vcpus
  ghv.vcpus = static_cast<vcpu*>(ExAllocatePoolWithTag(
    NonPagedPoolNx, arr_size, 'fr0g'));

  if (!ghv.vcpus) {
    DbgPrint("[hv] Failed to allocate VCPUs.\n");
    return false;
  }

  DbgPrint("[hv] Allocated %u VCPUs (0x%zX bytes).\n", ghv.vcpu_count, arr_size);

  // zero-initialize the vcpu array
  memset(ghv.vcpus, 0, arr_size);

  auto& smr = ghv.shared_memory_region;

  // zero-initialize the shared memory region
  memset(&smr, 0, sizeof(smr));

  // add the VCPU array to the SMR since it is dynamically allocated...
  mark_shared_memory(ghv.vcpus, arr_size);

  // TODO: maybe dont hardcode this...
  ghv.kprocess_directory_table_base_offset = 0x28;

  DbgPrint("[hv] KPROCESS::DirectoryTableBase offset = 0x%zX.\n",
    ghv.kprocess_directory_table_base_offset);

  auto const ps_get_process_id = reinterpret_cast<uint8_t*>(PsGetProcessId);

  // mov rax, [rcx + OFFSET]
  // retn
  if (ps_get_process_id[0] != 0x48 ||
      ps_get_process_id[1] != 0x8B ||
      ps_get_process_id[2] != 0x81 ||
      ps_get_process_id[7] != 0xC3) {
    DbgPrint("[hv] Failed to get offset of EPROCESS::UniqueProcessId.\n");
    return false;
  }

  ghv.eprocess_unique_process_id_offset =
    *reinterpret_cast<uint32_t*>(ps_get_process_id + 3);

  DbgPrint("[hv] EPROCESS::UniqueProcessId offset = 0x%zX.\n",
    ghv.eprocess_unique_process_id_offset);

  ghv.system_eprocess = reinterpret_cast<uint8_t*>(PsInitialSystemProcess);

  DbgPrint("[hv] System EPROCESS = 0x%zX.\n",
    reinterpret_cast<size_t>(ghv.system_eprocess));

  // store the System cr3 value (found in the System EPROCESS structure)
  ghv.system_cr3 = *reinterpret_cast<cr3*>(ghv.system_eprocess +
    ghv.kprocess_directory_table_base_offset);

  DbgPrint("[hv] System CR3 = 0x%zX.\n", ghv.system_cr3.flags);

  prepare_host_page_tables();

  DbgPrint("[hv] Mapped all of physical memory to address 0x%zX.\n",
    reinterpret_cast<uint64_t>(host_physical_memory_base));

  return true;
}

// virtualize the current system
bool start() {
  // we need to be running at an IRQL below DISPATCH_LEVEL so
  // that KeSetSystemAffinityThreadEx takes effect immediately
  NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

  // virtualize every cpu
  for (unsigned long i = 0; i < ghv.vcpu_count; ++i) {
    // restrict execution to the specified cpu
    auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << i);

    if (!virtualize_cpu(&ghv.vcpus[i])) {
      // TODO: handle this bruh -_-
      KeRevertToUserAffinityThreadEx(orig_affinity);
      return false;
    }

    KeRevertToUserAffinityThreadEx(orig_affinity);
  }

  return true;
}

// mark a region of kernel memory as shared (present in both host and guest)
bool mark_shared_memory(void* const start, size_t const size) {
  if (size <= 0)
    return false;

  auto& smr = ghv.shared_memory_region;

  if (smr.count >= smr.max_region_count)
    return false;

  smr.regions[smr.count++] = {
    reinterpret_cast<uint8_t*>(start),
    size
  };

  return true;
}

} // namespace hv

