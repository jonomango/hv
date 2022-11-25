#include <iostream>
#include <Windows.h>

#include "hv.h"

int main() {
  // check to see if the hypervisor is loaded...
  if (hv::ping() != hv::hypervisor_signature) {
    printf("Failed to ping the hypervisor... :(\n");
    return 0;
  }

  printf("Pinged the hypervisor!\n");

  getchar();
}

