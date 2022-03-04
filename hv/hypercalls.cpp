#include "hypercalls.h"
#include "vcpu.h"
#include "vmx.h"
#include "mm.h"
#include "exception-routines.h"

namespace hv::hc {

// ping the hypervisor to make sure it is running
void ping(vcpu* const cpu) {
  cpu->ctx->rax = hypervisor_signature;

  // we want to hide our vm-exit latency since the hypervisor uses this
  // hypercall for measuring vm-exit latency and we want to follow the
  // same code path as close as possible.
  cpu->hide_vm_exit_latency = true;

  skip_instruction();
}

// read arbitrary physical memory
void read_phys_mem(vcpu* const cpu) {
  // virtual address
  auto const dst  = reinterpret_cast<void*>(cpu->ctx->rdx);

  // physical address
  auto const src  = cpu->ctx->r8;

  // size in bytes
  auto const size = cpu->ctx->r9;

  host_exception_info e;
  memcpy_safe(e, dst, host_physical_memory_base + src, size);

  if (e.exception_occurred) {
    if (e.vector == page_fault) {
      // TODO: reflect #PF into guest
      inject_hw_exception(general_protection, 0);
      return;
    }

    // something bad happened :(
    inject_hw_exception(general_protection, 0);
    return;
  }

  skip_instruction();
}

} // namespace hv::hc

