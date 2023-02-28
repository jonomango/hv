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

  int info[4];
  uint64_t fish[10];

  double monkey = 0.0;

  for (int i = 0; i < 10; ++i) {
    // measure the time it takes to execute CPUID
    auto const start = __rdtsc();
    __cpuidex(info, 0, 0);
    auto const end = __rdtsc();

    fish[i] = end - start;

    // ADDING A SLEEP BREAKS IT WHATTTTTT?
    Sleep(1);

    //for (int i = 0; i < 100000; ++i)
      //monkey += sqrt((double)i);

    //__cpuidex(info, 0, 0);
  }

  for (int i = 0; i < 10; ++i)
    printf("%zu %i\n", fish[i], (int)monkey);

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

