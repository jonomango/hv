#pragma once

#include <ia32.hpp>

namespace hv {

inline constexpr segment_selector host_cs_selector = { 0, 0, 2 };
inline constexpr segment_selector host_fs_selector = { 0, 0, 1 };
inline constexpr segment_selector host_gs_selector = { 0, 0, 1 };
inline constexpr segment_selector host_tr_selector = { 0, 0, 8 };

// represents the host global descriptor table
struct gdt {
  // host GDT limit is set to 0xFFFF on vm-exit
  static constexpr size_t num_descriptors = 8192;

  // this descriptor array is what the GDTR points to
  alignas(8) segment_descriptor_32 descriptors[num_descriptors];
};

// initialize the host GDT and populate every descriptor
void prepare_gdt(gdt& gdt);

} // namespace hv

