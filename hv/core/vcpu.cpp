#include "vcpu.h"
#include "vmcs.h"
#include "guest-context.h"

#include "../util/mm.h"
#include "../util/arch.h"

namespace hv {

// defined in vm-launch.asm
extern bool __vm_launch();

// virtualize the current cpu
// note, this assumes that execution is already restricted to the desired cpu
bool vcpu::virtualize() {
  if (!check_vmx_capabilities())
    return false;

  enable_vmx_operation();

  auto vmxon_phys = get_physical(&vmxon_);
  NT_ASSERT(vmxon_phys % 0x1000 == 0);

  // enter vmx operation
  if (__vmx_on(&vmxon_phys) != 0) {
    // TODO: cleanup
    return false;
  }

  // 3.28.3.3.4
  __vmx_invept(invept_all_context, {});

  DbgPrint("[hv] entered vmx operation.\n");

  if (!set_vmcs_pointer()) {
    // TODO: cleanup

    __vmx_off();
    return false;
  }

  DbgPrint("[hv] set vmcs pointer.\n");

  // we dont want to break on any msr access
  memset(&msr_bitmap_, 0, sizeof(msr_bitmap_));

  // initialize the vmcs fields
  write_ctrl_vmcs_fields();
  write_host_vmcs_fields();
  write_guest_vmcs_fields();

  DbgPrint("[hv] initialized vmcs fields.\n");

  // launch the virtual machine
  if (!__vm_launch()) {
    DbgPrint("[hv] vmlaunch failed, error = %lli.\n", __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR));

    // TODO: cleanup

    __vmx_off();
    return false;
  }

  DbgPrint("[hv] virtualized cpu #%i\n", KeGetCurrentProcessorIndex());

  return true;
}

// check if VMX operation is supported
bool vcpu::check_vmx_capabilities() const {
  cpuid_eax_01 cpuid;
  __cpuid(reinterpret_cast<int*>(&cpuid), 1);

  // 3.23.6
  if (!cpuid.cpuid_feature_information_ecx.virtual_machine_extensions)
    return false;

  ia32_feature_control_register msr;
  msr.flags = __readmsr(IA32_FEATURE_CONTROL);

  // 3.23.7
  if (!msr.lock_bit || !msr.enable_vmx_outside_smx)
    return false;

  return true;
}

// perform certain actions that are required before entering vmx operation
void vcpu::enable_vmx_operation() {
  _disable();

  auto cr0 = __readcr0();
  auto cr4 = __readcr4();

  // 3.23.7
  cr4 |= CR4_VMX_ENABLE_FLAG;

  // 3.23.8
  cr0 |= __readmsr(IA32_VMX_CR0_FIXED0);
  cr0 &= __readmsr(IA32_VMX_CR0_FIXED1);
  cr4 |= __readmsr(IA32_VMX_CR4_FIXED0);
  cr4 &= __readmsr(IA32_VMX_CR4_FIXED1);

  __writecr0(cr0);
  __writecr4(cr4);

  _enable();

  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.11.5
  vmxon_.revision_id = vmx_basic.vmcs_revision_id;
  vmxon_.must_be_zero = 0;
}

// set the working-vmcs pointer to point to our vmcs structure
bool vcpu::set_vmcs_pointer() {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.2
  vmcs_.revision_id = vmx_basic.vmcs_revision_id;
  vmcs_.shadow_vmcs_indicator = 0;

  auto vmcs_phys = get_physical(&vmcs_);
  NT_ASSERT(vmcs_phys % 0x1000 == 0);

  if (__vmx_vmclear(&vmcs_phys) != 0)
    return false;

  if (__vmx_vmptrld(&vmcs_phys) != 0)
    return false;

  return true;
}

// function that is called on every vm-exit
void vcpu::handle_vm_exit(struct guest_context* ctx) {
  vmx_vmexit_reason exit_reason;
  exit_reason.flags = static_cast<uint32_t>(__vmx_vmread(VMCS_EXIT_REASON));

  if (exit_reason.basic_exit_reason == VMX_EXIT_REASON_MOV_CR) {
    vmx_exit_qualification_mov_cr qf;
    qf.flags = __vmx_vmread(VMCS_EXIT_QUALIFICATION);

    if (qf.access_type == VMX_EXIT_QUALIFICATION_ACCESS_MOV_TO_CR) {
      NT_ASSERT(qf.control_register == 3);
      switch (qf.general_purpose_register) {
      case VMX_EXIT_QUALIFICATION_GENREG_RAX: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rax & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RCX: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rcx & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RDX: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rdx & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RBX: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rbx & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RSP: __vmx_vmwrite(VMCS_GUEST_CR3, __vmx_vmread(VMCS_GUEST_RSP) & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RBP: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rbp & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RSI: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rsi & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RDI: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->rdi & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R8:  __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r8 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R9:  __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r9 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R10: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r10 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R11: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r11 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R12: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r12 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R13: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r13 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R14: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r14 & ~(1ull << 63)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R15: __vmx_vmwrite(VMCS_GUEST_CR3, ctx->r15 & ~(1ull << 63)); break;
      }

      __vmx_vmwrite(VMCS_GUEST_RIP, __vmx_vmread(VMCS_GUEST_RIP) + __vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH));
      return;
    } else if (qf.access_type == VMX_EXIT_QUALIFICATION_ACCESS_MOV_FROM_CR) {
      NT_ASSERT(qf.control_register == 3);
      switch (qf.general_purpose_register) {
      case VMX_EXIT_QUALIFICATION_GENREG_RAX: ctx->rax = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RCX: ctx->rcx = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RDX: ctx->rdx = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RBX: ctx->rbx = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RSP: __vmx_vmwrite(VMCS_GUEST_RSP, __vmx_vmread(VMCS_GUEST_CR3)); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RBP: ctx->rbp = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RSI: ctx->rsi = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_RDI: ctx->rdi = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R8:  ctx->r8 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R9:  ctx->r9 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R10: ctx->r10 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R11: ctx->r11 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R12: ctx->r12 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R13: ctx->r13 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R14: ctx->r14 = __vmx_vmread(VMCS_GUEST_CR3); break;
      case VMX_EXIT_QUALIFICATION_GENREG_R15: ctx->r15 = __vmx_vmread(VMCS_GUEST_CR3); break;
      }

      __vmx_vmwrite(VMCS_GUEST_RIP, __vmx_vmread(VMCS_GUEST_RIP) + __vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH));
      return;
    }
  } else if (exit_reason.basic_exit_reason == VMX_EXIT_REASON_EXECUTE_CPUID) {
    int regs[4];

    __cpuidex(regs, ctx->eax, ctx->ecx);

    ctx->eax = regs[0];
    ctx->ebx = regs[1];
    ctx->ecx = regs[2];
    ctx->edx = regs[3];

    __vmx_vmwrite(VMCS_GUEST_RIP, __vmx_vmread(VMCS_GUEST_RIP) + __vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH));
    return;
  } else if (exit_reason.basic_exit_reason == VMX_EXIT_REASON_EXECUTE_RDMSR || exit_reason.basic_exit_reason == VMX_EXIT_REASON_EXECUTE_WRMSR) {
    vmentry_interrupt_information interrupt_info{};
    interrupt_info.flags            = 0;
    interrupt_info.vector           = 3;
    interrupt_info.interruption_type = hardware_exception;
    interrupt_info.deliver_error_code = 1;
    interrupt_info.valid            = 1;

    __vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
    __vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE, 0);

    return;
  }

  DbgPrint("[hv] vm-exit occurred.\n");
  DbgPrint("[hv]   rip         = 0x%p\n", __vmx_vmread(VMCS_GUEST_RIP));
  DbgPrint("[hv]   exit_reason = 0x%p\n", &exit_reason);
  DbgPrint("[hv]   ctx         = 0x%p\n", ctx);

  __debugbreak();
}

} // namespace hv
