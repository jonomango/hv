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
    feature_control.lock_bit               = 1;
    feature_control.enable_vmx_inside_smx  = 0;
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

void emulate_xsetbv(vcpu*) {
  inject_hw_exception(general_protection, 0);
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
  cr0 new_cr0;
  new_cr0.flags = read_guest_gpr(vcpu, gpr);

  // TODO: check for exceptions

  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, new_cr0.flags);

  // make sure to account for VMX reserved bits when setting the real CR0
  new_cr0.flags |= vcpu->cdata()->vmx_cr0_fixed0;
  new_cr0.flags &= vcpu->cdata()->vmx_cr0_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR0, new_cr0.flags);
}

void emulate_mov_to_cr3(vcpu* const vcpu, uint64_t const gpr) {
  cr3 new_cr3;
  new_cr3.flags = read_guest_gpr(vcpu, gpr);

  // we want to read the CR4 value that the guest believes is active
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
  cr4 new_cr4;
  new_cr4.flags = read_guest_gpr(vcpu, gpr);

  // TODO: check for exceptions

  vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, new_cr4.flags);

  // make sure to account for VMX reserved bits when setting the real CR4
  new_cr4.flags |= vcpu->cdata()->vmx_cr4_fixed0;
  new_cr4.flags &= vcpu->cdata()->vmx_cr4_fixed1;

  vmx_vmwrite(VMCS_GUEST_CR4, new_cr4.flags);
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

