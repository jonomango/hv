#include "exit-handlers.h"
#include "guest-context.h"
#include "vcpu.h"
#include "vmx.h"

namespace hv {

void emulate_cpuid(guest_context* const ctx) {
  int regs[4];
  __cpuidex(regs, ctx->eax, ctx->ecx);

  ctx->eax = regs[0];
  ctx->ebx = regs[1];
  ctx->ecx = regs[2];
  ctx->edx = regs[3];

  skip_instruction();
}

void emulate_rdmsr(guest_context* ctx) {
  if (ctx->ecx == IA32_FEATURE_CONTROL) {
    ia32_feature_control_register feature_control;

    // TODO: cache this value since it can never change if the lock bit is 1.
    feature_control.flags = __readmsr(IA32_FEATURE_CONTROL);

    // spoof IA32_FEATURE_CONTROL to look like VMX is disabled
    feature_control.lock_bit               = 1;
    feature_control.enable_vmx_inside_smx  = 0;
    feature_control.enable_vmx_outside_smx = 0;

    ctx->rax = feature_control.flags & 0xFFFF'FFFF;
    ctx->rdx = feature_control.flags >> 32;

    skip_instruction();
    return;
  }

  inject_hw_exception(general_protection, 0);
}

void emulate_wrmsr(guest_context*) {
  // inject a #GP(0) for every invalid MSR write + IA32_FEATURE_CONTROL
  inject_hw_exception(general_protection, 0);
}

void emulate_getsec(guest_context*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_invd(guest_context*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_xsetbv(guest_context*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_vmxon(guest_context*) {
  // we are spoofing the value of the IA32_FEATURE_CONTROL MSR in
  // order to convince the guest that VMX has been disabled by BIOS.
  inject_hw_exception(general_protection, 0);
}

void emulate_vmcall(guest_context*) {
  inject_hw_exception(invalid_opcode);
}

void emulate_mov_to_cr0(guest_context*, uint64_t) {
  inject_hw_exception(general_protection, 0);
}

void emulate_mov_to_cr3(guest_context* const ctx, uint64_t const gpr) {
  // read the new value of cr3 from the specified general-purpose register
  cr3 new_cr3;
  if (gpr == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    new_cr3.flags = vmx_vmread(VMCS_GUEST_RSP);
  else
    new_cr3.flags = ctx->gpr[gpr];

  // 3.26.3.1.1
  new_cr3.flags &= ~(1ull << 63);

  // 3.28.4.3.3
  invvpid_descriptor desc;
  desc.linear_address = 0;
  desc.reserved1      = 0;
  desc.reserved2      = 0;
  desc.vpid           = guest_vpid;
  vmx_invvpid(invvpid_single_context_retaining_globals, desc);

  // it is now safe to write the new guest cr3
  vmx_vmwrite(VMCS_GUEST_CR3, new_cr3.flags);

  skip_instruction();
}

void emulate_mov_to_cr4(guest_context*, uint64_t) {
  inject_hw_exception(general_protection, 0);
}

void emulate_mov_from_cr0(guest_context*, uint64_t) {
  inject_hw_exception(general_protection, 0);
}

void emulate_mov_from_cr3(guest_context* const ctx, uint64_t const gpr) {
  // copy the guest CR3 to the specified general-purpose register
  if (gpr == VMX_EXIT_QUALIFICATION_GENREG_RSP)
    vmx_vmwrite(VMCS_GUEST_RSP, vmx_vmread(VMCS_GUEST_CR3));
  else
    ctx->gpr[gpr] = vmx_vmread(VMCS_GUEST_CR3);

  skip_instruction();
}

void emulate_mov_from_cr4(guest_context*, uint64_t) {
  inject_hw_exception(general_protection, 0);
}

void emulate_clts(guest_context* const) {
  inject_hw_exception(general_protection, 0);
}

void emulate_lmsw(guest_context* const) {
  inject_hw_exception(general_protection, 0);
}

void handle_mov_cr(guest_context* const ctx) {
  vmx_exit_qualification_mov_cr qualification;
  qualification.flags = vmx_vmread(VMCS_EXIT_QUALIFICATION);

  switch (qualification.access_type) {
  // MOV CRn, XXX
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_TO_CR:
    switch (qualification.control_register) {
    case VMX_EXIT_QUALIFICATION_REGISTER_CR0:
      emulate_mov_to_cr0(ctx, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR3:
      emulate_mov_to_cr3(ctx, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR4:
      emulate_mov_to_cr4(ctx, qualification.general_purpose_register);
      break;
    }
    break;
  // MOV XXX, CRn
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_FROM_CR:
    switch (qualification.control_register) {
    case VMX_EXIT_QUALIFICATION_REGISTER_CR0:
      emulate_mov_from_cr0(ctx, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR3:
      emulate_mov_from_cr3(ctx, qualification.general_purpose_register);
      break;
    case VMX_EXIT_QUALIFICATION_REGISTER_CR4:
      emulate_mov_from_cr4(ctx, qualification.general_purpose_register);
      break;
    }
    break;
  // CLTS
  case VMX_EXIT_QUALIFICATION_ACCESS_CLTS:
    emulate_clts(ctx);
    break;
  // LMSW XXX
  case VMX_EXIT_QUALIFICATION_ACCESS_LMSW:
    emulate_lmsw(ctx);
    break;
  }
}

void handle_nmi_window(guest_context*) {
  auto ctrl = read_ctrl_proc_based();
  ctrl.nmi_window_exiting = 0;
  write_ctrl_proc_based(ctrl);

  // reflect the NMI back into the guest
  inject_nmi();
}

void handle_exception_or_nmi(guest_context*) {
  auto ctrl = read_ctrl_proc_based();
  ctrl.nmi_window_exiting = 1;
  write_ctrl_proc_based(ctrl);
}

} // namespace hv

