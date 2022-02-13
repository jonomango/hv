#pragma once

#include <ia32.hpp>

namespace hv {

// initialize the host IDT and populate every descriptor
void prepare_host_idt(segment_descriptor_interrupt_gate_64* idt);

} // namespace hv

