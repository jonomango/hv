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

// read virtual memory from another process
void read_virt_mem(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // arguments
  cr3 guest_cr3;
  guest_cr3.flags      = ctx->rcx;
  auto const dst       = reinterpret_cast<uint8_t*>(ctx->rdx);
  auto const src       = reinterpret_cast<uint8_t*>(ctx->r8);
  auto const size      = ctx->r9;

  for (size_t bytes_read = 0; bytes_read < size;) {
    size_t dst_remaining = 0, src_remaining = 0;

    auto const curr_dst = gva2hva(dst + bytes_read, &dst_remaining);
    auto const curr_src = gva2hva(guest_cr3, src + bytes_read, &src_remaining);

    if (!curr_dst || !curr_src) {
      inject_hw_exception(general_protection, 0);
      return;
    }

    // the maximum allowed size that we can read at once with the translated HVAs
    auto const curr_size = min(size - bytes_read, min(dst_remaining, src_remaining));

    host_exception_info e;
    memcpy_safe(e, curr_dst, curr_src, curr_size);

    if (e.exception_occurred) {
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  skip_instruction();
}

} // namespace hv::hc

