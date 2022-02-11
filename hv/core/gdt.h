#pragma once

#include <ia32.hpp>

namespace hv {

// initialize the host GDT and populate every descriptor
void prepare_host_gdt(segment_descriptor_32* gdt, uint64_t tss_base);

} // namespace hv

