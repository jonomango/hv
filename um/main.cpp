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

  int info[4];
  uint64_t fish[10];
  unsigned int fish2[10];
  unsigned int fish3[10];

  double monkey = 0.0;

  for (int i = 0; i < 10; ++i) {
    // measure the time it takes to execute CPUID
    auto const start = __rdtscp(&fish2[i]);
    //auto const start = __rdtsc();
    __cpuidex(info, 0, 0);
    //auto const end = __rdtsc();
    auto const end = __rdtscp(&fish3[i]);

    fish[i] = end - start;

    // ADDING A SLEEP BREAKS IT WHATTTTTT?
    Sleep(1);

    //for (int i = 0; i < 100000; ++i)
      //monkey += sqrt((double)i);

    //__cpuidex(info, 0, 0);
  }

  for (int i = 0; i < 10; ++i)
    printf("%zu %i %u %u\n", fish[i], (int)monkey, fish2[i], fish3[i]);

  printf("Pinged the hypervisor! Flushing logs...\n");

  hv::test();

  while (true) {
    uint32_t count = 512;
    hv::logger_msg msgs[512];
    hv::flush_logs(count, msgs);

    for (uint32_t i = 0; i < count; ++i) {
      auto const& msg = msgs[i];
      printf("%s\n", msg.data);
    }
    for (uint32_t i = 0; i < count; ++i)
      printf("[%u][%I64u][%I64u] %s\n", msgs[i].aux, msgs[i].id, msgs[i].tsc, msgs[i].data);

    Sleep(1);
  }

  getchar();
}

