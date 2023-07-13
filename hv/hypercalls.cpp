#include "hypercalls.h"
#include "vcpu.h"
#include "vmx.h"
#include "mm.h"
#include "hv.h"
#include "exception-routines.h"
#include "introspection.h"

// first byte at the start of the image
extern "C" uint8_t __ImageBase;

namespace hv::hc {

// ping the hypervisor to make sure it is running
void ping(vcpu* const cpu) {
  cpu->ctx->rax = hypervisor_signature;

  skip_instruction();
}

// a hypercall for quick testing
void test(vcpu* const cpu) {
  char image_name[16];
  current_guest_image_file_name(image_name);

  HV_LOG_INFO("IMAGEBASE:      %p.", &__ImageBase);
  HV_LOG_INFO("IMAGENAME:      %s.", image_name);
  HV_LOG_INFO("KPCR:           %p.", current_guest_kpcr());
  HV_LOG_INFO("EPROCESS:       %p.", current_guest_eprocess());
  HV_LOG_INFO("ETHREAD:        %p.", current_guest_ethread());
  HV_LOG_INFO("PID:            %p.", current_guest_pid());
  HV_LOG_INFO("CPL:            %u.", current_guest_cpl());
  HV_LOG_INFO("EPT USED PAGES: %u / %u.", 
    static_cast<uint32_t>(cpu->ept.num_used_free_pages),
    static_cast<uint32_t>(ept_free_page_count));

  skip_instruction();
}

// devirtualize the current VCPU
void unload(vcpu* const cpu) {
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
  cr3 guest_cr3 = ghv.system_cr3;
  if (ctx->rcx)
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
  cr3 guest_cr3 = ghv.system_cr3;
  if (ctx->rcx)
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

  cpu->ctx->rax = 0;

  // ActiveProcessLinks is right after UniqueProcessId in memory
  auto const apl_offset = ghv.eprocess_unique_process_id_offset + 8;
  auto const head = ghv.system_eprocess + apl_offset;
  auto curr_entry = head;

  // iterate over every EPROCESS in the APL linked list
  do {
    // get the next entry in the linked list
    if (sizeof(curr_entry) != read_guest_virtual_memory(ghv.system_cr3,
        curr_entry + offsetof(LIST_ENTRY, Flink), &curr_entry, sizeof(curr_entry)))
      break;

    // EPROCESS
    auto const process = curr_entry - apl_offset;

    // EPROCESS::UniqueProcessId
    uint64_t pid = 0;
    if (sizeof(pid) != read_guest_virtual_memory(ghv.system_cr3,
        process + ghv.eprocess_unique_process_id_offset, &pid, sizeof(pid)))
      break;

    // we found the target process
    if (target_pid == pid) {
      // EPROCESS::DirectoryTableBase
      uint64_t cr3 = 0;
      if (sizeof(cr3) != read_guest_virtual_memory(ghv.system_cr3,
          process + ghv.kprocess_directory_table_base_offset, &cr3, sizeof(cr3)))
        break;

      cpu->ctx->rax = cr3;
      break;
    }
  } while (curr_entry != head);

  skip_instruction();
}

// install an EPT hook for the CURRENT logical processor ONLY
void install_ept_hook(vcpu* const cpu) {
  // arguments
  auto const orig_page_pfn = cpu->ctx->rcx;
  auto const exec_page_pfn = cpu->ctx->rdx;

  cpu->ctx->rax = install_ept_hook(cpu->ept, orig_page_pfn, exec_page_pfn);

  skip_instruction();
}

// remove a previously installed EPT hook
void remove_ept_hook(vcpu* const cpu) {
  // arguments
  auto const orig_page_pfn = cpu->ctx->rcx;

  remove_ept_hook(cpu->ept, orig_page_pfn);

  skip_instruction();
}

// flush the hypervisor logs into a buffer
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

// translate a virtual address to its physical address
void get_physical_address(vcpu* const cpu) {
  auto guest_cr3 = ghv.system_cr3;

  // use the system CR3 if none is provided
  if (cpu->ctx->rcx)
    guest_cr3.flags = cpu->ctx->rcx;

  cpu->ctx->rax = gva2gpa(guest_cr3, reinterpret_cast<void*>(cpu->ctx->rdx));

  skip_instruction();
}

// hide a physical page from the guest
void hide_physical_page(vcpu* const cpu) {
  auto const pfn = cpu->ctx->rcx;
  auto const pte = get_ept_pte(cpu->ept, pfn << 12, true);

  // this can occur if we failed to split the PDE
  if (!pte) {
    cpu->ctx->rax = 0;
    skip_instruction();
    return;
  }

  pte->page_frame_number = cpu->ept.dummy_page_pfn;
  vmx_invept(invept_all_context, {});

  cpu->ctx->rax = 1;
  skip_instruction();
}

// unhide a physical page from the guest
void unhide_physical_page(vcpu* const cpu) {
  auto const pfn = cpu->ctx->rcx;
  auto const pte = get_ept_pte(cpu->ept, pfn << 12, false);

  // this can occur if we never hid the page in the first place
  if (!pte) {
    skip_instruction();
    return;
  }

  pte->page_frame_number = pfn;
  vmx_invept(invept_all_context, {});

  skip_instruction();
}

// get the base address of the hypervisor
void get_hv_base(vcpu* const cpu) {
  cpu->ctx->rax = reinterpret_cast<uint64_t>(&__ImageBase);
  skip_instruction();
}

// write to the logger whenever a certain physical memory range is accessed
void install_mmr(vcpu* const cpu) {
  auto const phys = cpu->ctx->rcx;
  auto const size = static_cast<uint32_t>(cpu->ctx->rdx);
  auto const mode = static_cast<mmr_memory_mode>(cpu->ctx->r8 & 0b111);

  // return null by default
  cpu->ctx->rax = 0;

  // TODO: check for overlap with EPT hooking

  vcpu_ept_mmr_entry* entry = nullptr;

  for (auto& e : cpu->ept.mmr) {
    if (e.size != 0)
      continue;

    entry = &e;
    break;
  }

  // all entries are in use
  if (!entry) {
    skip_instruction();
    return;
  }

  entry->mode  = mode;
  entry->start = phys;
  entry->size  = size;

  for (auto addr = phys; addr < phys + size; addr += 0x1000) {
    auto const pte = get_ept_pte(cpu->ept, addr, true);
    if (!pte) {
      // TODO: properly handle errors, i.e. restore previous PTE permissions
      skip_instruction();
      return;
    }

    pte->read_access    = !(mode & mmr_memory_mode_r);
    pte->write_access   = !(mode & mmr_memory_mode_w);
    pte->execute_access = !(mode & mmr_memory_mode_x);

    // write access but no read access will generate an EPT misconfiguration
    if (pte->write_access && !pte->read_access)
      pte->write_access = 0;
  }

  vmx_invept(invept_all_context, {});

  cpu->ctx->rax = reinterpret_cast<uint64_t>(entry);
  skip_instruction();
}

// remove a monitored memory range
void remove_mmr(vcpu* cpu) {
  auto const entry = reinterpret_cast<vcpu_ept_mmr_entry*>(cpu->ctx->rcx);

  for (auto addr = entry->start; addr < entry->start + entry->size; addr += 0x1000) {
    auto const pte = get_ept_pte(cpu->ept, addr, true);
    if (!pte) {
      // TODO: properly handle errors, i.e. restore previous PTE permissions
      skip_instruction();
      return;
    }

    pte->read_access    = 1;
    pte->write_access   = 1;
    pte->execute_access = 1;
  }

  entry->size = 0;
  vmx_invept(invept_all_context, {});

  skip_instruction();
}

// remove every installed MMR
void remove_all_mmrs(vcpu* const cpu) {
  for (auto& entry : cpu->ept.mmr) {
    if (entry.size == 0)
      continue;

    for (auto addr = entry.start; addr < entry.start + entry.size; addr += 0x1000) {
      auto const pte = get_ept_pte(cpu->ept, addr, true);
      if (!pte) {
        // TODO: properly handle errors, i.e. restore previous PTE permissions
        skip_instruction();
        return;
      }

      pte->read_access    = 1;
      pte->write_access   = 1;
      pte->execute_access = 1;
    }

    entry.size = 0;
  }

  vmx_invept(invept_all_context, {});
  skip_instruction();
}

} // namespace hv::hc

