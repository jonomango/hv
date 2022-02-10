#pragma once

#include <ia32.hpp>

namespace hv {

// 3.6.14.1
struct interrupt_gate_descriptor_64 {
  union {
    struct {
      uint16_t offset_low; // 0-15
      uint16_t segment_selector; // 16-31

      uint32_t interrupt_stack_table : 3; // 0-2
      uint32_t must_be_zero_1 : 5; // 3-7
      uint32_t type : 4; // 8-11
      uint32_t must_be_zero_2 : 1; // 12
      uint32_t descriptor_privilege_level : 2; // 13-14
      uint32_t present : 1; // 15
      uint32_t offset_middle : 16; // 16-31

      uint32_t offset_high;
      uint32_t reserved;
    };
    struct {
      uint64_t flags_low;
      uint64_t flags_high;
    };
  };
};

// represents the host interrupt descriptor table
struct host_idt {
  alignas(8) interrupt_gate_descriptor_64 descriptors[256];
};

// initialize the host IDT and populate every descriptor
void prepare_host_idt(host_idt& idt);

} // namespace hv

