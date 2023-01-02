#include <iostream>
#include <Windows.h>

#include "hv.h"

int main() {
  // check to see if the hypervisor is loaded...
  __try {
    if (hv::ping() != hv::hypervisor_signature) {
      printf("Failed to ping the hypervisor... :(\n");
      getchar();
      return 0;
    }
  } __except (1) {
    printf("Failed to ping the hypervisor... :(\n");
    getchar();
    return 0;
  }

  printf("Pinged the hypervisor! Flushing logs...\n");

  while (true) {
    uint32_t count = 128;
    hv::logger_msg msgs[128];
    hv::flush_logs(count, msgs);

    for (uint32_t i = 0; i < count; ++i)
      printf("[%u] %s\n", msgs[i].id, msgs[i].data);

    Sleep(10);
  }

  getchar();
}

