#include <iostream>
#include <Windows.h>

#include "hv.h"

int main() {
  // check to see if the hypervisor is loaded...
  __try {
    if (hv::ping() != hv::hypervisor_signature) {
      printf("Failed to ping the hypervisor... :(\n");
      getchar();
      //return 0;
    }
  } __except (1) {
    printf("Failed to ping the hypervisor... :(\n");
    getchar();
    //return 0;
  }

  int info[4];
  int fish[10];

  for (int i = 0; i < 10; ++i) {
    _mm_lfence();
    auto const start = __rdtsc();
    _mm_lfence();

    __cpuidex(info, 0, 0);

    _mm_lfence();
    auto const end = __rdtsc();
    _mm_lfence();

    fish[i] = end - start;

    // ADDING A SLEEP BREAKS IT WHATTTTTT?
    Sleep(1);
  }

  for (int i = 0; i < 10; ++i)
    printf("%i\n", fish[i]);

  printf("Pinged the hypervisor! Flushing logs...\n");

  while (true) {
    uint32_t count = 128;
    hv::logger_msg msgs[128];
    hv::flush_logs(count, msgs);

    for (uint32_t i = 0; i < count; ++i)
      printf("[%u] %s\n", msgs[i].id, msgs[i].data);

    Sleep(1);
  }

  getchar();
}

