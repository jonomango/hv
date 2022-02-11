#pragma once

#include <ia32.hpp>

namespace hv {

#pragma pack(push, 4)

// TODO: move this into ia32
// 3.7.7
struct task_state_segment_64 {
  uint32_t reserved0;
  uint64_t rsp[3];
  uint64_t ist[8];
  uint32_t reserved1[2];
  uint16_t reserved2;
  uint16_t io_map_base_address;
};

#pragma pack(pop)

static_assert(sizeof(task_state_segment_64) == 0x68);

} // namespace hv

