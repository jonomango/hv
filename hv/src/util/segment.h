#pragma once

#include <ia32.hpp>

namespace hv {

// calculate a segment's base address
uint64_t segment_base(
  segment_descriptor_register_64 const& gdtr,
  segment_selector selector);

uint64_t segment_base(
  segment_descriptor_register_64 const& gdtr,
  uint16_t selector);

// calculate a segment's access rights
vmx_segment_access_rights segment_access(
  segment_descriptor_register_64 const& gdtr,
  segment_selector selector);

vmx_segment_access_rights segment_access(
  segment_descriptor_register_64 const& gdtr,
  uint16_t selector);

} // namespace hv

