#include "segment.h"

namespace hv {

// calculate a segment's base address
uint64_t segment_base(
    segment_descriptor_register_64 const& gdtr,
    segment_selector const selector) {
  // null selector
  if (selector.index == 0)
    return 0;

  // fetch the segment descriptor from the gdtr
  auto const descriptor = reinterpret_cast<segment_descriptor_64*>(
    gdtr.base_address + static_cast<uint64_t>(selector.index) * 8);

  // 3.3.4.5
  // calculate the segment base address
  auto base_address = 
    (uint64_t)descriptor->base_address_low |
    ((uint64_t)descriptor->base_address_middle << 16) |
    ((uint64_t)descriptor->base_address_high << 24);

  // 3.3.5.2
  // system descriptors are expanded to 16 bytes for ia-32e
  if (descriptor->descriptor_type == SEGMENT_DESCRIPTOR_TYPE_SYSTEM)
    base_address |= (uint64_t)descriptor->base_address_upper << 32;

  return base_address;
}

uint64_t segment_base(
    segment_descriptor_register_64 const& gdtr,
    uint16_t const selector) {
  segment_selector s;
  s.flags = selector;
  return segment_base(gdtr, s);
}

// calculate a segment's access rights
vmx_segment_access_rights segment_access(
    segment_descriptor_register_64 const& gdtr,
    segment_selector const selector) {
  // fetch the segment descriptor from the gdtr
  auto const descriptor = reinterpret_cast<segment_descriptor_64*>(
    gdtr.base_address + static_cast<uint64_t>(selector.index) * 8);

  vmx_segment_access_rights access;
  access.flags = 0;

  // 3.24.4.1
  access.type                       = descriptor->type;
  access.descriptor_type            = descriptor->descriptor_type;
  access.descriptor_privilege_level = descriptor->descriptor_privilege_level;
  access.present                    = descriptor->present;
  access.available_bit              = descriptor->system;
  access.long_mode                  = descriptor->long_mode;
  access.default_big                = descriptor->default_big;
  access.granularity                = descriptor->granularity;
  access.unusable                   = (selector.index == 0);

  return access;
}

vmx_segment_access_rights segment_access(
    segment_descriptor_register_64 const& gdtr,
    uint16_t const selector) {
  segment_selector s;
  s.flags = selector;
  return segment_access(gdtr, s);
}

} // namespace hv
