#include <iostream>
#include <Windows.h>
#include <intrin.h>

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

  hv::test();

  printf("Pinged the hypervisor! Flushing logs...\n");

  while (true) {
    // flush the logs
    uint32_t count = 512;
    hv::logger_msg msgs[512];
    hv::flush_logs(count, msgs);

    // print the logs
    for (uint32_t i = 0; i < count; ++i)
      printf("[%u][%I64u][%I64u] %s\n", msgs[i].aux, msgs[i].id, msgs[i].tsc, msgs[i].data);

    Sleep(1);
  }

  getchar();
}

