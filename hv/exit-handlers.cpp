#include "exit-handlers.h"
#include "guest-context.h"
#include "vcpu.h"
#include "vmx.h"

namespace hv {

namespace {

uint64_t read_guest_gpr(vcpu* const cpu,
  uint64_t const gpr) {
  if (gpr == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    return vmx_vmread(VMCS_GUEST_RSP);
  return cpu->ctx()->gpr[gpr];
}

void write_guest_gpr(vcpu* const cpu,
  uint64_t const gpr, uint64_t const value) {
  if (gpr == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    vmx_vmwrite(VMCS_GUEST_RSP, value);
  else
    cpu->ctx()->gpr[gpr] = value;
}

} // namespace

void emulate_cpuid(vcpu* const cpu) {
  int regs[4];
  __cpuidex(regs, cpu->ctx()->eax, cpu->ctx()->ecx);

  cpu->ctx()->rax = regs[0];
  cpu->ctx()->rbx = regs[1];
  cpu->ctx()->rcx = regs[2];
  cpu->ctx()->rdx = regs[3];

  skip_instruction();
}

void emulate_rdmsr(vcpu* const cpu) {
  if (cpu->ctx()->ecx == IA32_FEATURE_CONTROL) {
    auto feature_control = cpu->cdata()->feature_control;

    feature_control.lock_bit               = 1;

    // disable VMX
    feature_control.enable_vmx_inside_smx  = 0;
    feature_control.enable_vmx_outside_smx = 0;

    // disable SMX
    feature_control.senter_local_function_enables = 0;
    feature_control.senter_global_enable          = 0;

    cpu->ctx()->rax = feature_control.flags & 0xFFFF'FFFF;
    cpu->ctx()->rdx = feature_control.flags >> 32;

    skip_instruction();
    return;
  }

  inject_hw_exception(general_protection, 0);
}

void emulate_wrmsr(vcpu*) {
  // inject a #GP(0) for every invalid MSR write + IA32_FEATURE_CONTROL
  inject_hw_exception(general_protection, 0);
}

void emulate_getsec(vcpu*) {
  // inject a #GP(0) since SMX is disabled in the IA32_FEATURE_CONTROL MSR
  inject_hw_exception(general_protection, 0);
}

void emulate_invd(vcpu*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_xsetbv(vcpu* const cpu) {
  // 3.2.6

  xcr0 new_xcr0;
  new_xcr0.flags = (cpu->ctx()->rdx << 32) | cpu->ctx()->eax;

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

  // only XCR0 is supported
  if (cpu->ctx()->ecx != 0) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if trying to set an unsupported bit
  if (new_xcr0.flags & cpu->cdata()->xcr0_unsupported_mask) {
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
    inject_hw_exception(general_protection);
    return;
  }

  // #GP(0) if XCR0.AVX is clear and XCR0.opmask, XCR0.ZMM_Hi256, or XCR0.Hi16_ZMM is set
  if (!new_xcr0.avx && (new_xcr0.opmask || new_xcr0.zmm_hi256 || new_xcr0.zmm_hi16)) {
    inject_hw_exception(general_protection);
    return;
  }

  // #GP(0) if setting XCR0.BNDREG or XCR0.BNDCSR while not setting the other
  if (new_xcr0.bndreg != new_xcr0.bndcsr) {
    inject_hw_exception(general_protection);
    return;
  }

  // #GP(0) if setting XCR0.opmask, XCR0.ZMM_Hi256, or XCR0.Hi16_ZMM while not setting all of them
  if (new_xcr0.opmask != new_xcr0.zmm_hi256 || new_xcr0.zmm_hi256 != new_xcr0.zmm_hi16) {
    inject_hw_exception(general_protection);
    return;
  }

  _xsetbv(cpu->ctx()->ecx, new_xcr0.flags);

  skip_instruction();
}

void emulate_vmxon(vcpu*) {
  // we are spoofing the value of the IA32_FEATURE_CONTROL MSR in
  // order to convince the guest that VMX has been disabled by BIOS.
  inject_hw_exception(general_protection, 0);
}

void handle_vmcall(vcpu*) {
  inject_hw_exception(invalid_opcode);
}

void emulate_mov_to_cr0(vcpu* const cpu, uint64_t const gpr) {
  // 2.4.3
  // 3.2.5
  // 3.4.10.1
  // 3.26.3.2.1

  cr0 new_cr0;
  new_cr0.flags = read_guest_gpr(cpu, gpr);

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

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
  if (!new_cr0.write_protect && curr_cr4.cet_enable) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  cr0 host_cr0;
  host_cr0.flags = vmx_vmread(VMCS_HOST_CR0);

  if (new_cr0.cache_disable != host_cr0.cache_disable ||
      new_cr0.not_write_through != host_cr0.not_write_through) {
    // TODO:
    //   if CR0.CD or CR0.NW is modified, we need to update the host CR0
    //   since these bits are shared by the guest AND the host... i think?
    //   3.26.3.2.1
  }

  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, new_cr0.flags);

  // make sure to account for VMX reserved bits when setting the real CR0
  new_cr0.flags |= cpu->cdata()->vmx_cr0_fixed0;
  new_cr0.flags &= cpu->cdata()->vmx_cr0_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR0, new_cr0.flags);

  skip_instruction();
}

void emulate_mov_to_cr3(vcpu* const cpu, uint64_t const gpr) {
  cr3 new_cr3;
  new_cr3.flags = read_guest_gpr(cpu, gpr);

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

  bool invalidate_tlb = true;

  // 3.4.10.4.1
  if (curr_cr4.pcid_enable && (new_cr3.flags & (1ull << 63))) {
    invalidate_tlb = false;
    new_cr3.flags &= ~(1ull << 63);
  }

  // a mask where bits [63:MAXPHYSADDR] are set to 1
  auto const reserved_mask = ~((1ull << cpu->cdata()->max_phys_addr) - 1);

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

  skip_instruction();
}

void emulate_mov_to_cr4(vcpu* const cpu, uint64_t const gpr) {
  // 2.4.3
  // 2.6.2.1
  // 3.2.5
  // 3.4.10.1
  // 3.4.10.4.1

  cr4 new_cr4;
  new_cr4.flags = read_guest_gpr(cpu, gpr);

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

  cr3 curr_cr3;
  curr_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);

  cr0 curr_cr0;
  curr_cr0.flags = vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW);

  // #GP(0) if an attempt is made to set CR4.SMXE when SMX is not supported
  if (!cpu->cdata()->cpuid_01.cpuid_feature_information_ecx.safer_mode_extensions
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
  if (new_cr4.la57_enable) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if CR4.CET == 1 and CR0.WP == 0
  if (new_cr4.cet_enable && !curr_cr0.write_protect) {
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
  new_cr4.flags |= cpu->cdata()->vmx_cr4_fixed0;
  new_cr4.flags &= cpu->cdata()->vmx_cr4_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR4, new_cr4.flags);

  skip_instruction();
}

void emulate_mov_from_cr3(vcpu* const cpu, uint64_t const gpr) {
  write_guest_gpr(cpu, gpr, vmx_vmread(VMCS_GUEST_CR3));
  skip_instruction();
}

void emulate_clts(vcpu*) {
  // clear CR0.TS in the read shadow
  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, 
    vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW) & ~CR0_TASK_SWITCHED_FLAG);

  // clear CR0.TS in the real CR0 register
  vmx_vmwrite(VMCS_GUEST_CR0,
    vmx_vmread(VMCS_GUEST_CR0) & ~CR0_TASK_SWITCHED_FLAG);

  skip_instruction();
}

void emulate_lmsw(vcpu*, uint16_t const value) {
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

void handle_nmi_window(vcpu*) {
  auto ctrl = read_ctrl_proc_based();
  ctrl.nmi_window_exiting = 0;
  write_ctrl_proc_based(ctrl);

  // reflect the NMI back into the guest
  inject_nmi();
}

void handle_exception_or_nmi(vcpu*) {
  auto ctrl = read_ctrl_proc_based();
  ctrl.nmi_window_exiting = 1;
  write_ctrl_proc_based(ctrl);
}

void handle_vmx_instruction(vcpu*) {
  // inject #UD for every VMX instruction since we
  // don't allow the guest to ever enter VMX operation.
  inject_hw_exception(invalid_opcode);
}

} // namespace hv

