#include <iostream>

#include "hv.h"
#include "dumper.h"

int main() {
  if (!hv::is_hv_running()) {
    printf("HV not running.\n");
    return 0;
  }

  auto const hv_base = static_cast<uint8_t*>(hv::get_hv_base());
  auto const hv_size = 0x64000;

  // hide the hypervisor
  hv::for_each_cpu([&](uint32_t) {
    for (size_t i = 0; i < hv_size; i += 0x1000) {
      auto const virt = hv_base + i;
      auto const phys = hv::get_physical_address(0, virt);

      if (!phys) {
        printf("Failed to get physical address for 0x%p.\n", virt);
        continue;
      }

      if (!hv::hide_physical_page(phys >> 12))
        printf("Failed to hide physical page: 0x%p.\n", virt);
    }
  });

  auto cr3 = hv::query_process_cr3(3012);
  auto phys = hv::get_physical_address(cr3, (void*)(0x7ff716820000 + 0x1000));

  printf("Physical address: %zX\n", phys);

  hv::for_each_cpu([&](uint32_t) {
    hv::install_mmr(phys, 0x1, hv::mmr_memory_mode_r);
  });

  printf("Pinged the hypervisor! Flushing logs...\n");

  while (!GetAsyncKeyState(VK_RETURN)) {
    // flush the logs
    uint32_t count = 512;
    hv::logger_msg msgs[512];
    hv::flush_logs(count, msgs);

    // print the logs
    for (uint32_t i = 0; i < count; ++i)
      printf("[%I64u][CPU=%u] %s\n", msgs[i].id, msgs[i].aux, msgs[i].data);

    Sleep(1);
  }

  hv::for_each_cpu([](uint32_t) {
    hv::remove_all_mmrs();
  });
}

