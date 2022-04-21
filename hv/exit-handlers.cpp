#include "exit-handlers.h"
#include "guest-context.h"
#include "exception-routines.h"
#include "hypercalls.h"
#include "vcpu.h"
#include "vmx.h"

namespace hv {

void emulate_cpuid(vcpu* const cpu) {
  auto const ctx = cpu->ctx;

  int regs[4];
  __cpuidex(regs, ctx->eax, ctx->ecx);

  ctx->rax = regs[0];
  ctx->rbx = regs[1];
  ctx->rcx = regs[2];
  ctx->rdx = regs[3];

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_rdmsr(vcpu* const cpu) {
  if (cpu->ctx->ecx == IA32_FEATURE_CONTROL) {
    // return the fake guest FEATURE_CONTROL MSR
    cpu->ctx->rax = cpu->cached.guest_feature_control.flags & 0xFFFF'FFFF;
    cpu->ctx->rdx = cpu->cached.guest_feature_control.flags >> 32;

    cpu->hide_vm_exit_overhead = true;
    skip_instruction();
    return;
  }

  // inject #GP(0) for reserved MSRs
  inject_hw_exception(general_protection, 0);
  return;
}

void emulate_wrmsr(vcpu* const cpu) {
  auto const msr = cpu->ctx->ecx;
  auto const value = (cpu->ctx->rdx << 32) | cpu->ctx->eax;

  // we need to make sure to update EPT memory types if the guest
  // modifies any of the MTRR registers
  if (msr == IA32_MTRR_DEF_TYPE     || msr == IA32_MTRR_FIX64K_00000 ||
      msr == IA32_MTRR_FIX16K_80000 || msr == IA32_MTRR_FIX16K_A0000 ||
     (msr >= IA32_MTRR_FIX4K_C0000  && msr <= IA32_MTRR_FIX4K_F8000) ||
     (msr >= IA32_MTRR_PHYSBASE0    && msr <= IA32_MTRR_PHYSBASE0 + 511)) {
    // let the guest write to the (shared) MTRRs
    host_exception_info e;
    wrmsr_safe(e, msr, value);

    if (e.exception_occurred) {
      inject_hw_exception(general_protection, 0);
      return;
    }

    // update EPT memory types (if CR0.CD isn't set)
    if (!read_effective_guest_cr0().cache_disable) {
      update_ept_memory_type(cpu->ept);
      vmx_invept(invept_all_context, {});
    }

    cpu->hide_vm_exit_overhead = true;
    skip_instruction();
    return;
  }

  // reserved MSR
  inject_hw_exception(general_protection, 0);
  return;
}

void emulate_getsec(vcpu*) {
  // inject a #GP(0) since SMX is disabled in the IA32_FEATURE_CONTROL MSR
  inject_hw_exception(general_protection, 0);
}

void emulate_invd(vcpu*) {
  // TODO: properly implement INVD (can probably make a very small stub
  //       that flushes specific cacheline entries prior to executing INVD)
  inject_hw_exception(general_protection, 0);
}

void emulate_xsetbv(vcpu* const cpu) {
  // 3.2.6

  xcr0 new_xcr0;
  new_xcr0.flags = (cpu->ctx->rdx << 32) | cpu->ctx->eax;

  auto const curr_cr4 = read_effective_guest_cr4();

  // only XCR0 is supported
  if (cpu->ctx->ecx != 0) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if trying to set an unsupported bit
  if (new_xcr0.flags & cpu->cached.xcr0_unsupported_mask) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if clearing XCR0.X87
  if (!new_xcr0.x87) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if XCR0.AVX is 1 while XCRO.SSE is cleared
  if (new_xcr0.avx && !new_xcr0.sse) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if XCR0.AVX is clear and XCR0.opmask, XCR0.ZMM_Hi256, or XCR0.Hi16_ZMM is set
  if (!new_xcr0.avx && (new_xcr0.opmask || new_xcr0.zmm_hi256 || new_xcr0.zmm_hi16)) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if setting XCR0.BNDREG or XCR0.BNDCSR while not setting the other
  if (new_xcr0.bndreg != new_xcr0.bndcsr) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if setting XCR0.opmask, XCR0.ZMM_Hi256, or XCR0.Hi16_ZMM while not setting all of them
  if (new_xcr0.opmask != new_xcr0.zmm_hi256 || new_xcr0.zmm_hi256 != new_xcr0.zmm_hi16) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  host_exception_info e;
  xsetbv_safe(e, cpu->ctx->ecx, new_xcr0.flags);

  if (e.exception_occurred) {
    // TODO: assert that it was a #GP(0) that occurred, although I really
    //       doubt that any other exception could happen (according to manual).
    inject_hw_exception(general_protection, 0);
    return;
  }

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_vmxon(vcpu*) {
  // usually a #UD doesn't trigger a vm-exit, but in this case it is possible
  // that CR4.VMXE is 1 while guest shadow CR4.VMXE is 0.
  if (!read_effective_guest_cr4().vmx_enable) {
    inject_hw_exception(invalid_opcode);
    return;
  }

  // we are spoofing the value of the IA32_FEATURE_CONTROL MSR in
  // order to convince the guest that VMX has been disabled by BIOS.
  inject_hw_exception(general_protection, 0);
}

void emulate_vmcall(vcpu* const cpu) {
  auto const code = cpu->ctx->rax & 0xFF;
  auto const key  = cpu->ctx->rax >> 8;

  // validate the hypercall key
  if (key != hypercall_key) {
    inject_hw_exception(invalid_opcode);
    return;
  }

  // handle the hypercall
  switch (code) {
  case hypercall_ping:              hc::ping(cpu);              return;
  case hypercall_test:              hc::test(cpu);              return;
  case hypercall_read_virt_mem:     hc::read_virt_mem(cpu);     return;
  case hypercall_write_virt_mem:    hc::write_virt_mem(cpu);    return;
  case hypercall_query_process_cr3: hc::query_process_cr3(cpu); return;
  }

  inject_hw_exception(invalid_opcode);
}

void handle_vmx_preemption(vcpu*) {
  // do nothing.
}

void emulate_mov_to_cr0(vcpu* const cpu, uint64_t const gpr) {
  // 2.4.3
  // 3.2.5
  // 3.4.10.1
  // 3.26.3.2.1

  cr0 new_cr0;
  new_cr0.flags = read_guest_gpr(cpu->ctx, gpr);

  auto const curr_cr0 = read_effective_guest_cr0();
  auto const curr_cr4 = read_effective_guest_cr4();

  // CR0[15:6] is always 0
  new_cr0.reserved1 = 0;

  // CR0[17] is always 0
  new_cr0.reserved2 = 0;

  // CR0[28:19] is always 0
  new_cr0.reserved3 = 0;

  // CR0.ET is always 1
  new_cr0.extension_type = 1;

  // #GP(0) if setting any reserved bits in CR0[63:32]
  if (new_cr0.reserved4) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if setting CR0.PG while CR0.PE is clear
  if (new_cr0.paging_enable && !new_cr0.protection_enable) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if invalid bit combination
  if (!new_cr0.cache_disable && new_cr0.not_write_through) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if an attempt is made to clear CR0.PG
  if (!new_cr0.paging_enable) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if an attempt is made to clear CR0.WP while CR4.CET is set
  if (!new_cr0.write_protect && curr_cr4.control_flow_enforcement_enable) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  cr0 host_cr0;
  host_cr0.flags = vmx_vmread(VMCS_HOST_CR0);

  // guest tried to modify CR0.CD, which cannot be updated through VMCS_GUEST_CR0
  if (new_cr0.cache_disable != curr_cr0.cache_disable) {
    if (new_cr0.cache_disable)
      set_ept_memory_type(cpu->ept, MEMORY_TYPE_UNCACHEABLE);
    else
      update_ept_memory_type(cpu->ept);

    // invalidate old mappings since we've just updated the EPT structures
    vmx_invept(invept_all_context, {});
  }

  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, new_cr0.flags);

  // make sure to account for VMX reserved bits when setting the real CR0
  new_cr0.flags |= cpu->cached.vmx_cr0_fixed0;
  new_cr0.flags &= cpu->cached.vmx_cr0_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR0, new_cr0.flags);

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_mov_to_cr3(vcpu* const cpu, uint64_t const gpr) {
  cr3 new_cr3;
  new_cr3.flags = read_guest_gpr(cpu->ctx, gpr);

  auto const curr_cr4 = read_effective_guest_cr4();

  bool invalidate_tlb = true;

  // 3.4.10.4.1
  if (curr_cr4.pcid_enable && (new_cr3.flags & (1ull << 63))) {
    invalidate_tlb = false;
    new_cr3.flags &= ~(1ull << 63);
  }

  // a mask where bits [63:MAXPHYSADDR] are set to 1
  auto const reserved_mask = ~((1ull << cpu->cached.max_phys_addr) - 1);

  // 3.2.5
  if (new_cr3.flags & reserved_mask) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // 3.28.4.3.3
  if (invalidate_tlb) {
    invvpid_descriptor desc;
    desc.linear_address = 0;
    desc.reserved1      = 0;
    desc.reserved2      = 0;
    desc.vpid           = guest_vpid;
    vmx_invvpid(invvpid_single_context_retaining_globals, desc);
  }

  // it is now safe to write the new guest cr3
  vmx_vmwrite(VMCS_GUEST_CR3, new_cr3.flags);

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_mov_to_cr4(vcpu* const cpu, uint64_t const gpr) {
  // 2.4.3
  // 2.6.2.1
  // 3.2.5
  // 3.4.10.1
  // 3.4.10.4.1

  cr4 new_cr4;
  new_cr4.flags = read_guest_gpr(cpu->ctx, gpr);

  cr3 curr_cr3;
  curr_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);

  auto const curr_cr0 = read_effective_guest_cr0();
  auto const curr_cr4 = read_effective_guest_cr4();

  // #GP(0) if an attempt is made to set CR4.SMXE when SMX is not supported
  if (!cpu->cached.cpuid_01.cpuid_feature_information_ecx.safer_mode_extensions
      && new_cr4.smx_enable) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if an attempt is made to write a 1 to any reserved bits
  if (new_cr4.reserved1 || new_cr4.reserved2) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if an attempt is made to change CR4.PCIDE from 0 to 1 while CR3[11:0] != 000H
  if ((new_cr4.pcid_enable && !curr_cr4.pcid_enable) && (curr_cr3.flags & 0xFFF)) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if CR4.PAE is cleared
  if (!new_cr4.physical_address_extension) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if CR4.LA57 is enabled
  if (new_cr4.linear_addresses_57_bit) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if CR4.CET == 1 and CR0.WP == 0
  if (new_cr4.control_flow_enforcement_enable && !curr_cr0.write_protect) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // invalidate TLB entries if required
  if (new_cr4.page_global_enable != curr_cr4.page_global_enable ||
      !new_cr4.pcid_enable && curr_cr4.pcid_enable ||
      new_cr4.smep_enable && !curr_cr4.smep_enable) {
    invvpid_descriptor desc;
    desc.linear_address = 0;
    desc.reserved1      = 0;
    desc.reserved2      = 0;
    desc.vpid           = guest_vpid;
    vmx_invvpid(invvpid_single_context, desc);
  }
  
  vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, new_cr4.flags);

  // make sure to account for VMX reserved bits when setting the real CR4
  new_cr4.flags |= cpu->cached.vmx_cr4_fixed0;
  new_cr4.flags &= cpu->cached.vmx_cr4_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR4, new_cr4.flags);

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_mov_from_cr3(vcpu* const cpu, uint64_t const gpr) {
  write_guest_gpr(cpu->ctx, gpr, vmx_vmread(VMCS_GUEST_CR3));

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_clts(vcpu* const cpu) {
  // clear CR0.TS in the read shadow
  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW,
    vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW) & ~CR0_TASK_SWITCHED_FLAG);

  // clear CR0.TS in the real CR0 register
  vmx_vmwrite(VMCS_GUEST_CR0,
    vmx_vmread(VMCS_GUEST_CR0) & ~CR0_TASK_SWITCHED_FLAG);

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void emulate_lmsw(vcpu* const cpu, uint16_t const value) {
  // 3.25.1.3

  cr0 new_cr0;
  new_cr0.flags = value;

  // update the guest CR0 read shadow
  cr0 shadow_cr0;
  shadow_cr0.flags = vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW);
  shadow_cr0.protection_enable   = new_cr0.protection_enable;
  shadow_cr0.monitor_coprocessor = new_cr0.monitor_coprocessor;
  shadow_cr0.emulate_fpu         = new_cr0.emulate_fpu;
  shadow_cr0.task_switched       = new_cr0.task_switched;
  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, shadow_cr0.flags);

  // update the real guest CR0.
  // we don't have to worry about VMX reserved bits since CR0.PE (the only
  // reserved bit) can't be cleared to 0 by the LMSW instruction while in
  // protected mode.
  cr0 real_cr0;
  real_cr0.flags = vmx_vmread(VMCS_GUEST_CR0);
  real_cr0.protection_enable   = new_cr0.protection_enable;
  real_cr0.monitor_coprocessor = new_cr0.monitor_coprocessor;
  real_cr0.emulate_fpu         = new_cr0.emulate_fpu;
  real_cr0.task_switched       = new_cr0.task_switched;
  vmx_vmwrite(VMCS_GUEST_CR0, real_cr0.flags);

  cpu->hide_vm_exit_overhead = true;
  skip_instruction();
}

void handle_mov_cr(vcpu* const cpu) {
  vmx_exit_qualification_mov_cr qualification;
  qualification.flags = vmx_vmread(VMCS_EXIT_QUALIFICATION);

  switch (qualification.access_type) {
  // MOV CRn, XXX
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_TO_CR:
    switch (qualification.control_register) {
    case VMX_EXIT_QUALIFICATION_REGISTER_CR0:
      emulate_mov_to_cr0(cpu, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR3:
      emulate_mov_to_cr3(cpu, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR4:
      emulate_mov_to_cr4(cpu, qualification.general_purpose_register);
      break;
    }
    break;
  // MOV XXX, CRn
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_FROM_CR:
    // TODO: assert that we're accessing CR3 (and not CR8)
    emulate_mov_from_cr3(cpu, qualification.general_purpose_register);
    break;
  // CLTS
  case VMX_EXIT_QUALIFICATION_ACCESS_CLTS:
    emulate_clts(cpu);
    break;
  // LMSW XXX
  case VMX_EXIT_QUALIFICATION_ACCESS_LMSW:
    emulate_lmsw(cpu, qualification.lmsw_source_data);
    break;
  }
}

void handle_nmi_window(vcpu* const cpu) {
  --cpu->queued_nmis;

  // inject the NMI into the guest
  inject_nmi();

  if (cpu->queued_nmis == 0) {
    // disable NMI-window exiting since we have no more NMIs to inject
    auto ctrl = read_ctrl_proc_based();
    ctrl.nmi_window_exiting = 0;
    write_ctrl_proc_based(ctrl);
  }
  
  // there is the possibility that a host NMI occurred right before we
  // disabled NMI-window exiting. make sure to re-enable it if this is the case.
  if (cpu->queued_nmis > 0) {
    auto ctrl = read_ctrl_proc_based();
    ctrl.nmi_window_exiting = 1;
    write_ctrl_proc_based(ctrl);
  }
}

void handle_exception_or_nmi(vcpu* const cpu) {
  // enqueue an NMI to be injected into the guest later on
  ++cpu->queued_nmis;

  auto ctrl = read_ctrl_proc_based();
  ctrl.nmi_window_exiting = 1;
  write_ctrl_proc_based(ctrl);
}

void handle_vmx_instruction(vcpu*) {
  // inject #UD for every VMX instruction since we
  // don't allow the guest to ever enter VMX operation.
  inject_hw_exception(invalid_opcode);
}

void handle_ept_violation(vcpu* const cpu) {
  vmx_exit_qualification_ept_violation qualification;
  qualification.flags = vmx_vmread(VMCS_EXIT_QUALIFICATION);

  // guest physical address that caused the ept-violation
  auto const physical_address = vmx_vmread(qualification.caused_by_translation ?
    VMCS_GUEST_PHYSICAL_ADDRESS : VMCS_EXIT_GUEST_LINEAR_ADDRESS);

  if (qualification.execute_access &&
     (qualification.write_access || qualification.read_access)) {
    // TODO: assert
    inject_hw_exception(machine_check);
    return;
  }

  auto const hook = find_ept_hook(cpu->ept.hooks, physical_address >> 12);

  if (!hook) {
    // TODO: assert
    inject_hw_exception(machine_check);
    return;
  }

  auto const pte = get_ept_pte(cpu->ept, physical_address);

  if (qualification.execute_access) {
    pte->read_access       = 0;
    pte->write_access      = 0;
    pte->execute_access    = 1;
    pte->page_frame_number = hook->exec_pfn;
  } else {
    pte->read_access       = 1;
    pte->write_access      = 1;
    pte->execute_access    = 0;
    pte->page_frame_number = hook->orig_pfn;
  }
}

} // namespace hv

