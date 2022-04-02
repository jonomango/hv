#include "hypercalls.h"
#include "vcpu.h"
#include "vmx.h"
#include "mm.h"
#include "hv.h"
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

// a hypercall for quick testing
void test(vcpu* const cpu) {
  // TODO: move the code for adding/removing ept hooks into ept.cpp

  auto const ctx = cpu->ctx;
  auto&      ept = cpu->ept;

  // arguments
  auto const orig_page = ctx->rcx;
  auto const exec_page = ctx->rdx;

  // used up every EPT hook already
  if (ept.num_ept_hooks >= ept_hook_count) {
    skip_instruction();
    return;
  }

  auto const pte = get_ept_pte(ept, orig_page, true);

  // failed to get the EPT PTE (PDE split probably failed)
  if (!pte) {
    skip_instruction();
    return;
  }

  ept.ept_hooks[ept.num_ept_hooks++] = {
    pte,
    orig_page >> 12,
    exec_page >> 12
  };

  // an EPT violation will be raised whenever the guest tries to execute
  // code in this page
  pte->execute_access = 0;

  skip_instruction();
}

// read from virtual memory from another process
void read_virt_mem(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // arguments
  cr3 guest_cr3;
  guest_cr3.flags = ctx->rcx;
  auto const dst  = reinterpret_cast<uint8_t*>(ctx->rdx);
  auto const src  = reinterpret_cast<uint8_t*>(ctx->r8);
  auto const size = ctx->r9;

  size_t bytes_read = 0;

  while (bytes_read < size) {
    size_t dst_remaining = 0, src_remaining = 0;

    // translate the guest virtual addresses into host virtual addresses.
    // this has to be done 1 page at a time. :(
    auto const curr_dst = gva2hva(dst + bytes_read, &dst_remaining);
    auto const curr_src = gva2hva(guest_cr3, src + bytes_read, &src_remaining);

    if (!curr_dst) {
      // guest virtual address that caused the fault
      ctx->cr2 = reinterpret_cast<uint64_t>(dst + bytes_read);

      page_fault_exception error;
      error.flags            = 0;
      error.present          = 0;
      error.write            = 1;
      error.user_mode_access = (current_guest_cpl() == 3);

      inject_hw_exception(page_fault, error.flags);
      return;
    }

    // this means that the target memory isn't paged in. there's nothing
    // we can do about that since we're not currently in that process's context.
    if (!curr_src)
      break;

    // the maximum allowed size that we can read at once with the translated HVAs
    auto const curr_size = min(size - bytes_read, min(dst_remaining, src_remaining));

    host_exception_info e;
    memcpy_safe(e, curr_dst, curr_src, curr_size);

    if (e.exception_occurred) {
      // this REALLY shouldn't happen... ever...
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  ctx->rax = bytes_read;
  skip_instruction();
}

// write to virtual memory from another process
void write_virt_mem(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // arguments
  cr3 guest_cr3;
  guest_cr3.flags = ctx->rcx;
  auto const dst  = reinterpret_cast<uint8_t*>(ctx->rdx);
  auto const src  = reinterpret_cast<uint8_t*>(ctx->r8);
  auto const size = ctx->r9;

  size_t bytes_read = 0;

  while (bytes_read < size) {
    size_t dst_remaining = 0, src_remaining = 0;

    // translate the guest virtual addresses into host virtual addresses.
    // this has to be done 1 page at a time. :(
    auto const curr_dst = gva2hva(dst + bytes_read, &dst_remaining);
    auto const curr_src = gva2hva(guest_cr3, src + bytes_read, &src_remaining);

    if (!curr_src) {
      // guest virtual address that caused the fault
      ctx->cr2 = reinterpret_cast<uint64_t>(src + bytes_read);

      page_fault_exception error;
      error.flags            = 0;
      error.present          = 0;
      error.write            = 0;
      error.user_mode_access = (current_guest_cpl() == 3);

      inject_hw_exception(page_fault, error.flags);
      return;
    }

    // this means that the target memory isn't paged in. there's nothing
    // we can do about that since we're not currently in that process's context.
    if (!curr_dst)
      break;

    // the maximum allowed size that we can read at once with the translated HVAs
    auto const curr_size = min(size - bytes_read, min(dst_remaining, src_remaining));

    host_exception_info e;
    memcpy_safe(e, curr_dst, curr_src, curr_size);

    if (e.exception_occurred) {
      // this REALLY shouldn't happen... ever...
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  ctx->rax = bytes_read;
  skip_instruction();
}

// get the kernel CR3 value of an arbitrary process
void query_process_cr3(vcpu* const cpu) {
  // PID of the process to get the CR3 value of
  auto const target_pid = cpu->ctx->rcx;

  // System process
  if (target_pid == 4) {
    cpu->ctx->rax = ghv.system_cr3.flags;
    skip_instruction();
    return;
  }

  // TODO: gva2hva

  // ActiveProcessLinks is right after UniqueProcessId in memory
  auto const apl_offset = ghv.eprocess_unique_process_id_offset + 8;
  auto const head = reinterpret_cast<LIST_ENTRY*>(ghv.system_eprocess + apl_offset);

  cpu->ctx->rax = 0;

  // iterate over every EPROCESS in the APL linked list (except the System process)
  for (auto curr_entry = head->Flink; curr_entry != head; curr_entry = curr_entry->Flink) {
    // EPROCESS address
    auto const process = reinterpret_cast<uint8_t*>(curr_entry) - apl_offset;

    auto const pid = *reinterpret_cast<uint64_t*>(
      process + ghv.eprocess_unique_process_id_offset);

    // not the gnome we're looking for :c
    if (target_pid != pid)
      continue;

    // process->DirectoryTableBase
    cpu->ctx->rax = *reinterpret_cast<uint64_t*>(
      process + ghv.kprocess_directory_table_base_offset);

    break;
  }

  skip_instruction();
}

} // namespace hv::hc

