#include "exit-handlers.h"

#include "../util/arch.h"
#include "../util/vmx.h"
#include "../util/guest-context.h"

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

void emulate_rdmsr(guest_context*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_wrmsr(guest_context*) {
  inject_hw_exception(general_protection, 0);
}

void emulate_mov_to_cr(guest_context* const ctx) {
  // TODO: cache this value somehow...
  vmx_exit_qualification_mov_cr qualification;
  qualification.flags = vmx_vmread(VMCS_EXIT_QUALIFICATION);
  auto const gpr_idx = qualification.general_purpose_register;

  cr3 new_cr3;

  // read the new value of cr3 from the specified general-purpose register
  if (gpr_idx != VMX_EXIT_QUALIFICATION_GENREG_RSP)
    new_cr3.flags = ctx->gp_regs[gpr_idx];
  else
    new_cr3.flags = vmx_vmread(VMCS_GUEST_RSP);

  new_cr3.flags &= ~(1ull << 63);

  // write the new guest CR3 value
  vmx_vmwrite(VMCS_GUEST_CR3, new_cr3.flags);

  skip_instruction();
}

void emulate_mov_from_cr(guest_context* const ctx) {
  vmx_exit_qualification_mov_cr qualification;
  qualification.flags = vmx_vmread(VMCS_EXIT_QUALIFICATION);
  auto const gpr_idx = qualification.general_purpose_register;

  cr3 current_cr3;
  current_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);

  // write current value of cr3 to the specified general-purpose register
  if (gpr_idx != VMX_EXIT_QUALIFICATION_GENREG_RSP)
    ctx->gp_regs[gpr_idx] = current_cr3.flags;
  else
    vmx_vmwrite(VMCS_GUEST_RSP, current_cr3.flags);

  skip_instruction();
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
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_TO_CR:   emulate_mov_to_cr(ctx);   break;
  case VMX_EXIT_QUALIFICATION_ACCESS_MOV_FROM_CR: emulate_mov_from_cr(ctx); break;
  case VMX_EXIT_QUALIFICATION_ACCESS_CLTS:        emulate_clts(ctx);        break;
  case VMX_EXIT_QUALIFICATION_ACCESS_LMSW:        emulate_lmsw(ctx);        break;
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

