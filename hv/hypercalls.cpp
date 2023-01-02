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

  skip_instruction();
}

// a hypercall for quick testing
void test(vcpu*) {
  skip_instruction();
}

// devirtualize the current VCPU
void unload(vcpu* cpu) {
  cpu->stop_virtualization = true;
  skip_instruction();
}

// read from arbitrary physical memory
void read_phys_mem(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // arguments
  auto const dst  = reinterpret_cast<uint8_t*>(ctx->rcx);
  auto const src  = host_physical_memory_base + ctx->rdx;
  auto const size = ctx->r8;

  size_t bytes_read = 0;

  while (bytes_read < size) {
    size_t dst_remaining = 0;

    // translate the guest buffer into hypervisor space
    auto const curr_dst = gva2hva(dst + bytes_read, &dst_remaining);

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

    auto const curr_size = min(dst_remaining, size - bytes_read);

    host_exception_info e;
    memcpy_safe(e, curr_dst, src + bytes_read, curr_size);

    if (e.exception_occurred) {
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  ctx->rax = bytes_read;
  skip_instruction();
}

// write to arbitrary physical memory
void write_phys_mem(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // arguments
  auto const dst  = host_physical_memory_base + ctx->rcx;
  auto const src  = reinterpret_cast<uint8_t*>(ctx->rdx);
  auto const size = ctx->r8;

  size_t bytes_read = 0;

  while (bytes_read < size) {
    size_t src_remaining = 0;

    // translate the guest buffer into hypervisor space
    auto const curr_src = gva2hva(src + bytes_read, &src_remaining);

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

    auto const curr_size = min(size - bytes_read, src_remaining);

    host_exception_info e;
    memcpy_safe(e, dst + bytes_read, curr_src, curr_size);

    if (e.exception_occurred) {
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  ctx->rax = bytes_read;
  skip_instruction();
}

// read from virtual memory in another process
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

// write to virtual memory in another process
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
    auto const curr_dst = gva2hva(guest_cr3, dst + bytes_read, &dst_remaining);
    auto const curr_src = gva2hva(src + bytes_read, &src_remaining);

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

// install an EPT hook for the CURRENT logical processor ONLY
void install_ept_hook(vcpu* const cpu) {
  // arguments
  auto const orig_page = cpu->ctx->rcx;
  auto const exec_page = cpu->ctx->rdx;

  cpu->ctx->rax = install_ept_hook(cpu->ept, orig_page >> 12, exec_page >> 12);

  skip_instruction();
}

// remove a previously installed EPT hook
void remove_ept_hook(vcpu* const cpu) {
  // arguments
  auto const orig_page = cpu->ctx->rcx;

  remove_ept_hook(cpu->ept, orig_page >> 12);

  skip_instruction();
}

// flush the hypervisor logs into a specified buffer
void flush_logs(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  // arguments
  uint32_t count = ctx->ecx;
  uint8_t* buffer = reinterpret_cast<uint8_t*>(ctx->rdx);

  ctx->eax = 0;

  if (count <= 0) {
    skip_instruction();
    return;
  }

  auto& l = ghv.logger;

  scoped_spin_lock lock(l.lock);

  count = min(count, l.msg_count);

  auto start = reinterpret_cast<uint8_t*>(&l.msgs[l.msg_start]);
  auto size = min(l.max_msg_count - l.msg_start, count) * sizeof(l.msgs[0]);

  // read the first chunk of logs before circling back around (if needed)
  for (size_t bytes_read = 0; bytes_read < size;) {
    size_t dst_remaining = 0;

    // translate the guest virtual address
    auto const curr_dst = gva2hva(buffer + bytes_read, &dst_remaining);

    if (!curr_dst) {
      // guest virtual address that caused the fault
      ctx->cr2 = reinterpret_cast<uint64_t>(buffer + bytes_read);

      page_fault_exception error;
      error.flags = 0;
      error.present = 0;
      error.write = 1;
      error.user_mode_access = (current_guest_cpl() == 3);

      inject_hw_exception(page_fault, error.flags);
      return;
    }

    // the maximum allowed size that we can read at once with the translated HVAs
    auto const curr_size = min(size - bytes_read, dst_remaining);

    host_exception_info e;
    memcpy_safe(e, curr_dst, start + bytes_read, curr_size);

    if (e.exception_occurred) {
      // this REALLY shouldn't happen... ever...
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  buffer += size;
  start = reinterpret_cast<uint8_t*>(&l.msgs[0]);
  size = (count * sizeof(l.msgs[0])) - size;

  for (size_t bytes_read = 0; bytes_read < size;) {
    size_t dst_remaining = 0;

    // translate the guest virtual address
    auto const curr_dst = gva2hva(buffer + bytes_read, &dst_remaining);

    if (!curr_dst) {
      // guest virtual address that caused the fault
      ctx->cr2 = reinterpret_cast<uint64_t>(buffer + bytes_read);

      page_fault_exception error;
      error.flags = 0;
      error.present = 0;
      error.write = 1;
      error.user_mode_access = (current_guest_cpl() == 3);

      inject_hw_exception(page_fault, error.flags);
      return;
    }

    // the maximum allowed size that we can read at once with the translated HVAs
    auto const curr_size = min(size - bytes_read, dst_remaining);

    host_exception_info e;
    memcpy_safe(e, curr_dst, start + bytes_read, curr_size);

    if (e.exception_occurred) {
      // this REALLY shouldn't happen... ever...
      inject_hw_exception(general_protection, 0);
      return;
    }

    bytes_read += curr_size;
  }

  l.msg_count -= count;
  l.msg_start = (l.msg_start + count) % l.max_msg_count;

  ctx->eax = count;

  skip_instruction();
}

} // namespace hv::hc

