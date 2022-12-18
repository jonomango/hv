#include <iostream>
#include <Windows.h>

#include "hv.h"

// format the provided arguments into a single null-terminated string
template <typename... Args>
void logger_fmt(size_t& length, char* buffer, Args&&...) {
  auto const max_length = length;
  length = 0;
}

int main() {
  char buffer[64];
  size_t length = 64;
  logger_fmt(length, buffer, "Hello world!");

  printf("buffer = [%s]\n", buffer);

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

  printf("Pinged the hypervisor!\n");

  int regs[4];
  __cpuidex(regs, 0, 0);

  getchar();
}

