#include "gdt.h"

#include "../util/mm.h"
#include "../util/arch.h"
#include "../util/segment.h"

namespace hv {

struct tss_descriptor_64 {
  uint64_t segment_limit_low : 16; // 0-15
  uint64_t base_address_low : 16; // 16-31

  uint64_t base_address_middle : 8; // 32-39
  uint64_t type : 4; // 40-43
  uint64_t descriptor_type : 1; // 44
  uint64_t descriptor_privilege_level : 2; // 45-46
  uint64_t present : 1; // 47
  uint64_t segment_limit_high : 4; // 48-51
  uint64_t system : 1; // 52
  uint64_t reserved1 : 2; // 53-54
  uint64_t granularity : 1; // 55
  uint64_t base_address_high : 8; // 56-63

  uint64_t base_address_upper : 32;
  uint64_t reserved2 : 32;
};

// initialize the host GDT and populate every descriptor
void prepare_gdt(gdt& gdt) {
  memset(gdt.descriptors, 0, sizeof(gdt.descriptors));

  // base-address fields dont need to be set since they are not used in
  // 64-bit mode (for CS) and since the TSS base-address field is loaded
  // directly from the VMCS. limits are also set by the processor on
  // vm-exit.
  segment_descriptor_register_64 gdtr;
  _sgdt(&gdtr);

  uint64_t tss_base = segment_base(gdtr, 0x40);

  memcpy(gdt.descriptors, (void*)gdtr.base_address, gdtr.limit + 1);

  // setup the CS segment descriptor
  auto& cs_desc = gdt.descriptors[host_cs_selector.index];
  cs_desc.type                       = SEGMENT_DESCRIPTOR_TYPE_CODE_EXECUTE_READ;
  cs_desc.descriptor_type            = SEGMENT_DESCRIPTOR_TYPE_CODE_OR_DATA;
  cs_desc.descriptor_privilege_level = 0;
  cs_desc.present                    = 1;
  cs_desc.long_mode                  = 1;
  cs_desc.default_big                = 0;
  cs_desc.granularity                = 0;

  // setup the TSS descriptor
  auto& tss_desc = *reinterpret_cast<tss_descriptor_64*>(
    &gdt.descriptors[host_tr_selector.index]);
  tss_desc.type                       = SEGMENT_DESCRIPTOR_TYPE_TSS_BUSY;
  tss_desc.descriptor_type            = SEGMENT_DESCRIPTOR_TYPE_SYSTEM;
  tss_desc.descriptor_privilege_level = 0;
  tss_desc.present                    = 1;
  tss_desc.granularity                = 0;

  tss_desc.base_address_low = (tss_base >> 0) & 0b1111'1111'1111'1111;
  tss_desc.base_address_middle = (tss_base >> 16) & 0b1111'1111;
  tss_desc.base_address_high = (tss_base >> 24) & 0b1111'1111;
  tss_desc.base_address_upper = (tss_base >> 32) & 0xFFFF'FFFF;

  tss_desc.segment_limit_low = 0x67;
}

} // namespace hv
