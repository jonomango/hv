#include "exit-handlers.h"
#include "guest-context.h"
#include "vcpu.h"
#include "vmx.h"

namespace hv {

namespace {

uint64_t read_guest_gpr(vcpu* const vcpu,
  uint64_t const gpr) {
  if (gpr == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    return vmx_vmread(VMCS_GUEST_RSP);
  return vcpu->ctx()->gpr[gpr];
}

void write_guest_gpr(vcpu* const vcpu,
  uint64_t const gpr, uint64_t const value) {
  if (gpr == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    vmx_vmwrite(VMCS_GUEST_RSP, value);
  else
    vcpu->ctx()->gpr[gpr] = value;
}

} // namespace

void emulate_cpuid(vcpu* const vcpu) {
  int regs[4];
  __cpuidex(regs, vcpu->ctx()->eax, vcpu->ctx()->ecx);

  vcpu->ctx()->rax = regs[0];
  vcpu->ctx()->rbx = regs[1];
  vcpu->ctx()->rcx = regs[2];
  vcpu->ctx()->rdx = regs[3];

  skip_instruction();
}

void emulate_rdmsr(vcpu* const vcpu) {
  if (vcpu->ctx()->ecx == IA32_FEATURE_CONTROL) {
    ia32_feature_control_register feature_control;

    // TODO: cache this value since it can never change if the lock bit is 1.
    feature_control.flags = __readmsr(IA32_FEATURE_CONTROL);

    // spoof IA32_FEATURE_CONTROL to look like VMX is disabled
    feature_control.lock_bit = 1;
    feature_control.enable_vmx_inside_smx = 0;
    feature_control.enable_vmx_outside_smx = 0;

    vcpu->ctx()->rax = feature_control.flags & 0xFFFF'FFFF;
    vcpu->ctx()->rdx = feature_control.flags >> 32;

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
  inject_hw_exception(general_protection, 0);
}

void emulate_invd(vcpu*) {
  inject_hw_exception(general_protection, 0);
}

// TODO: add to ia32
union xcr0 {
  struct {
    uint64_t X87 : 1; // 0
    uint64_t SSE : 1; // 1
    uint64_t AVX : 1; // 2
    uint64_t BNDREG : 1; // 3
    uint64_t BNDCSR : 1; // 4
    uint64_t opmask : 1; // 5
    uint64_t ZMM_Hi256 : 1; // 6
    uint64_t Hi16_ZMM : 1; // 7
    uint64_t reserved1 : 1;
    uint64_t PKRU : 1; // 9
  };

  uint64_t flags;
};

void emulate_xsetbv(vcpu* const vcpu) {
  // 3.2.6

  xcr0 new_xcr0;
  new_xcr0.flags = (vcpu->ctx()->rdx << 32) | vcpu->ctx()->eax;

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

  // only XCR0 is supported
  if (vcpu->ctx()->ecx != 0) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if trying to set an unsupported bit
  if (new_xcr0.flags & vcpu->cdata()->xcr0_unsupported_mask) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if clearing XCR0.X87
  if (!new_xcr0.X87) {
    inject_hw_exception(general_protection, 0);
    return;
  }

  // #GP(0) if XCR0.AVX is 1 while XCRO.SSE is cleared
  if (new_xcr0.AVX && !new_xcr0.SSE) {
    inject_hw_exception(general_protection);
    return;
  }

  // #GP(0) if XCR0.AVX is clear and XCR0.opmask, XCR0.ZMM_Hi256, or XCR0.Hi16_ZMM is set
  if (!new_xcr0.AVX && (new_xcr0.opmask || new_xcr0.ZMM_Hi256 || new_xcr0.Hi16_ZMM)) {
    inject_hw_exception(general_protection);
    return;
  }

  // #GP(0) if setting XCR0.BNDREG or XCR0.BNDCSR while not setting the other
  if (new_xcr0.BNDREG != new_xcr0.BNDCSR) {
    inject_hw_exception(general_protection);
    return;
  }

  // #GP(0) if setting XCR0.opmask, XCR0.ZMM_Hi256, or XCR0.Hi16_ZMM while not setting all of them
  if (new_xcr0.opmask != new_xcr0.ZMM_Hi256 || new_xcr0.ZMM_Hi256 != new_xcr0.Hi16_ZMM) {
    inject_hw_exception(general_protection);
    return;
  }

  _xsetbv(vcpu->ctx()->ecx, new_xcr0.flags);

  skip_instruction();
}

void emulate_vmxon(vcpu*) {
  // we are spoofing the value of the IA32_FEATURE_CONTROL MSR in
  // order to convince the guest that VMX has been disabled by BIOS.
  inject_hw_exception(general_protection, 0);
}

void emulate_vmcall(vcpu*) {
  inject_hw_exception(invalid_opcode);
}

void emulate_mov_to_cr0(vcpu* const vcpu, uint64_t const gpr) {
  // 2.4.3
  // 3.2.5
  // 3.4.10.1
  // 3.26.3.2.1

  cr0 new_cr0;
  new_cr0.flags = read_guest_gpr(vcpu, gpr);

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
  new_cr0.flags |= vcpu->cdata()->vmx_cr0_fixed0;
  new_cr0.flags &= vcpu->cdata()->vmx_cr0_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR0, new_cr0.flags);

  skip_instruction();
}

void emulate_mov_to_cr3(vcpu* const vcpu, uint64_t const gpr) {
  cr3 new_cr3;
  new_cr3.flags = read_guest_gpr(vcpu, gpr);

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

  bool invalidate_tlb = true;

  // 3.4.10.4.1
  if (curr_cr4.pcid_enable && (new_cr3.flags & (1ull << 63))) {
    invalidate_tlb = false;
    new_cr3.flags &= ~(1ull << 63);
  }

  // a mask where bits [63:MAXPHYSADDR] are set to 1
  auto const reserved_mask = ~((1ull << vcpu->cdata()->max_phys_addr) - 1);

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

void emulate_mov_to_cr4(vcpu* const vcpu, uint64_t const gpr) {
  // 2.4.3
  // 3.2.5
  // 3.4.10.1
  // 3.4.10.4.1

  cr4 new_cr4;
  new_cr4.flags = read_guest_gpr(vcpu, gpr);

  cr4 curr_cr4;
  curr_cr4.flags = vmx_vmread(VMCS_CTRL_CR4_READ_SHADOW);

  cr3 curr_cr3;
  curr_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);

  cr0 curr_cr0;
  curr_cr0.flags = vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW);

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
  new_cr4.flags |= vcpu->cdata()->vmx_cr4_fixed0;
  new_cr4.flags &= vcpu->cdata()->vmx_cr4_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR4, new_cr4.flags);

  skip_instruction();
}

void emulate_mov_from_cr3(vcpu* const vcpu, uint64_t const gpr) {
  write_guest_gpr(vcpu, gpr, vmx_vmread(VMCS_GUEST_CR3));
  skip_instruction();
}

void emulate_clts(vcpu*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_lmsw(vcpu*) {
  inject_hw_exception(general_protection, 0);
}

void handle_mov_cr(vcpu* const vcpu) {
  vmx_exit_qualification_mov_cr qualification;
  qualification.flags = vmx_vmread(VMCS_EXIT_QUALIFICATION);

  switch (qualification.access_type) {
  // MOV CRn, XXX
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_TO_CR:
    switch (qualification.control_register) {
    case VMX_EXIT_QUALIFICATION_REGISTER_CR0:
      emulate_mov_to_cr0(vcpu, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR3:
      emulate_mov_to_cr3(vcpu, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR4:
      emulate_mov_to_cr4(vcpu, qualification.general_purpose_register);
      break;
    }
    break;
  // MOV XXX, CRn
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_FROM_CR:
    // TODO: assert that we're accessing CR3 (and not CR8)
    emulate_mov_from_cr3(vcpu, qualification.general_purpose_register);
    break;
  // CLTS
  case VMX_EXIT_QUALIFICATION_ACCESS_CLTS:
    emulate_clts(vcpu);
    break;
  // LMSW XXX
  case VMX_EXIT_QUALIFICATION_ACCESS_LMSW:
    emulate_lmsw(vcpu);
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

} // namespace hv

