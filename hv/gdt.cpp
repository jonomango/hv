#include "gdt.h"
#include "vcpu.h"
#include "mm.h"

namespace hv {

// initialize the host GDT and populate every descriptor
void prepare_host_gdt(
    segment_descriptor_32* const gdt,
    task_state_segment_64 const* const tss) {
  memset(gdt, 0, host_gdt_descriptor_count * sizeof(gdt[0]));

  // setup the CS segment descriptor
  auto& cs_desc = gdt[host_cs_selector.index];
  cs_desc.type                       = SEGMENT_DESCRIPTOR_TYPE_CODE_EXECUTE_READ;
  cs_desc.descriptor_type            = SEGMENT_DESCRIPTOR_TYPE_CODE_OR_DATA;
  cs_desc.descriptor_privilege_level = 0;
  cs_desc.present                    = 1;
  cs_desc.long_mode                  = 1;
  cs_desc.default_big                = 0;
  cs_desc.granularity                = 0;

  // setup the TSS segment descriptor
  auto& tss_desc = *reinterpret_cast<segment_descriptor_64*>(
    &gdt[host_tr_selector.index]);
  tss_desc.type                       = SEGMENT_DESCRIPTOR_TYPE_TSS_BUSY;
  tss_desc.descriptor_type            = SEGMENT_DESCRIPTOR_TYPE_SYSTEM;
  tss_desc.descriptor_privilege_level = 0;
  tss_desc.present                    = 1;
  tss_desc.granularity                = 0;
  tss_desc.segment_limit_low          = 0x67;
  tss_desc.segment_limit_high         = 0;

  // point the TSS descriptor to our TSS -_-
  auto const base = reinterpret_cast<uint64_t>(tss);
  tss_desc.base_address_low    = (base >> 00) & 0xFFFF;
  tss_desc.base_address_middle = (base >> 16) & 0xFF;
  tss_desc.base_address_high   = (base >> 24) & 0xFF;
  tss_desc.base_address_upper  = (base >> 32) & 0xFFFFFFFF;
}

} // namespace hv
