#pragma once

#include <ia32.hpp>

namespace hv {

// number of available descriptor slots in the host IDT
inline constexpr size_t host_idt_descriptor_count = 256;

// initialize the host IDT and populate every descriptor
void prepare_host_idt(segment_descriptor_interrupt_gate_64* idt);

} // namespace hv

