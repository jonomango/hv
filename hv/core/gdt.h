#pragma once

#include <ia32.hpp>

namespace hv {

inline constexpr segment_selector host_cs_selector = { 0, 0, 2 };
inline constexpr segment_selector host_fs_selector = { 0, 0, 1 };
inline constexpr segment_selector host_gs_selector = { 0, 0, 1 };
inline constexpr segment_selector host_tr_selector = { 0, 0, 8 };

// represents the host global descriptor table
struct host_gdt {
  alignas(8) segment_descriptor_32 descriptors[8192];
};

// initialize the host GDT and populate every descriptor
void prepare_host_gdt(host_gdt& gdt);

} // namespace hv

