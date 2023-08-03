#include "vcpu.h"
#include "hv.h"
#include "gdt.h"
#include "idt.h"
#include "vmx.h"
#include "vmcs.h"
#include "timing.h"
#include "trap-frame.h"
#include "exit-handlers.h"
#include "exception-routines.h"
#include "introspection.h"

// first byte at the start of the image
extern "C" uint8_t __ImageBase;

namespace hv {

// defined in vm-launch.asm
bool vm_launch();

// cache certain fixed values (CPUID results, MSRs, etc) that are used
// frequently during VMX operation (to speed up vm-exit handling).
static void cache_cpu_data(vcpu_cached_data& cached) {
  __cpuid(reinterpret_cast<int*>(&cached.cpuid_01), 0x01);

  // VMX needs to be enabled to read from certain VMX_* MSRS
  if (!cached.cpuid_01.cpuid_feature_information_ecx.virtual_machine_extensions)
    return;

  cpuid_eax_80000008 cpuid_80000008;
  __cpuid(reinterpret_cast<int*>(&cpuid_80000008), 0x80000008);

  cached.max_phys_addr = cpuid_80000008.eax.number_of_physical_address_bits;

  cached.vmx_cr0_fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
  cached.vmx_cr0_fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);
  cached.vmx_cr4_fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
  cached.vmx_cr4_fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);

  cpuid_eax_0d_ecx_00 cpuid_0d;
  __cpuidex(reinterpret_cast<int*>(&cpuid_0d), 0x0D, 0x00);
  
  // features in XCR0 that are supported
  cached.xcr0_unsupported_mask = ~((static_cast<uint64_t>(
    cpuid_0d.edx.flags) << 32) | cpuid_0d.eax.flags);

  cached.feature_control.flags = __readmsr(IA32_FEATURE_CONTROL);
  cached.vmx_misc.flags        = __readmsr(IA32_VMX_MISC);

  // create a fake guest FEATURE_CONTROL MSR that has VMX and SMX disabled
  cached.guest_feature_control                               = cached.feature_control;
  cached.guest_feature_control.lock_bit                      = 1;
  cached.guest_feature_control.enable_vmx_inside_smx         = 0;
  cached.guest_feature_control.enable_vmx_outside_smx        = 0;
  cached.guest_feature_control.senter_local_function_enables = 0;
  cached.guest_feature_control.senter_global_enable          = 0;
}

// enable VMX operation prior to execution of the VMXON instruction
static bool enable_vmx_operation(vcpu const* const cpu) {
  // 3.23.6
  if (!cpu->cached.cpuid_01.cpuid_feature_information_ecx.virtual_machine_extensions) {
    DbgPrint("[hv] VMX not supported by CPUID.\n");
    return false;
  }

  // 3.23.7
  if (!cpu->cached.feature_control.lock_bit ||
      !cpu->cached.feature_control.enable_vmx_outside_smx) {
    DbgPrint("[hv] VMX not enabled outside SMX.\n");
    return false;
  }

  _disable();

  auto cr0 = __readcr0();
  auto cr4 = __readcr4();

  // 3.23.7
  cr4 |= CR4_VMX_ENABLE_FLAG;

  // 3.23.8
  cr0 |= cpu->cached.vmx_cr0_fixed0;
  cr0 &= cpu->cached.vmx_cr0_fixed1;
  cr4 |= cpu->cached.vmx_cr4_fixed0;
  cr4 &= cpu->cached.vmx_cr4_fixed1;

  __writecr0(cr0);
  __writecr4(cr4);

  _enable();

  return true;
}

// enter VMX operation by executing VMXON
static bool enter_vmx_operation(vmxon& vmxon_region) {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.11.5
  vmxon_region.revision_id = vmx_basic.vmcs_revision_id;
  vmxon_region.must_be_zero = 0;

  auto vmxon_phys = MmGetPhysicalAddress(&vmxon_region).QuadPart;
  NT_ASSERT(vmxon_phys % 0x1000 == 0);

  // enter vmx operation
  if (!vmx_vmxon(vmxon_phys)) {
    DbgPrint("[hv] VMXON failed.\n");
    return false;
  }

  // 3.28.3.3.4
  vmx_invept(invept_all_context, {});

  return true;
}

// load the VMCS pointer by executing VMPTRLD
static bool load_vmcs_pointer(vmcs& vmcs_region) {
  ia32_vmx_basic_register vmx_basic;
  vmx_basic.flags = __readmsr(IA32_VMX_BASIC);

  // 3.24.2
  vmcs_region.revision_id = vmx_basic.vmcs_revision_id;
  vmcs_region.shadow_vmcs_indicator = 0;

  auto vmcs_phys = MmGetPhysicalAddress(&vmcs_region).QuadPart;
  NT_ASSERT(vmcs_phys % 0x1000 == 0);

  if (!vmx_vmclear(vmcs_phys)) {
    DbgPrint("[hv] VMCLEAR failed.\n");
    return false;
  }

  if (!vmx_vmptrld(vmcs_phys)) {
    DbgPrint("[hv] VMPTRLD failed.\n");
    return false;
  }

  return true;
}

// enable vm-exits for MTRR MSR writes
static void enable_mtrr_exiting(vcpu* const cpu) {
  ia32_mtrr_capabilities_register mtrr_cap;
  mtrr_cap.flags = __readmsr(IA32_MTRR_CAPABILITIES);

  enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_DEF_TYPE, true);

  // enable exiting for fixed-range MTRRs
  if (mtrr_cap.fixed_range_supported) {
    enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_FIX64K_00000, true);
    enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_FIX16K_80000, true);
    enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_FIX16K_A0000, true);

    for (uint32_t i = 0; i < 8; ++i)
      enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_FIX4K_C0000 + i, true);
  }

  // enable exiting for variable-range MTRRs
  for (uint32_t i = 0; i < mtrr_cap.variable_range_count; ++i) {
    enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_PHYSBASE0 + i * 2, true);
    enable_exit_for_msr_write(cpu->msr_bitmap, IA32_MTRR_PHYSMASK0 + i * 2, true);
  }
}

// initialize external structures that are not included in the VMCS
static void prepare_external_structures(vcpu* const cpu) {
  memset(&cpu->msr_bitmap, 0, sizeof(cpu->msr_bitmap));
  enable_exit_for_msr_read(cpu->msr_bitmap, IA32_FEATURE_CONTROL, true);

  enable_mtrr_exiting(cpu);

  // we don't care about anything that's in the TSS
  memset(&cpu->host_tss, 0, sizeof(cpu->host_tss));

  prepare_host_idt(cpu->host_idt);
  prepare_host_gdt(cpu->host_gdt, &cpu->host_tss);

  prepare_ept(cpu->ept);
}

// call the appropriate exit-handler for this vm-exit
static void dispatch_vm_exit(vcpu* const cpu, vmx_vmexit_reason const reason) {
  switch (reason.basic_exit_reason) {
  case VMX_EXIT_REASON_EXCEPTION_OR_NMI:             handle_exception_or_nmi(cpu);     break;
  case VMX_EXIT_REASON_EXECUTE_GETSEC:               emulate_getsec(cpu);              break;
  case VMX_EXIT_REASON_EXECUTE_INVD:                 emulate_invd(cpu);                break;
  case VMX_EXIT_REASON_NMI_WINDOW:                   handle_nmi_window(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_CPUID:                emulate_cpuid(cpu);               break;
  case VMX_EXIT_REASON_MOV_CR:                       handle_mov_cr(cpu);               break;
  case VMX_EXIT_REASON_EXECUTE_RDMSR:                emulate_rdmsr(cpu);               break;
  case VMX_EXIT_REASON_EXECUTE_WRMSR:                emulate_wrmsr(cpu);               break;
  case VMX_EXIT_REASON_EXECUTE_XSETBV:               emulate_xsetbv(cpu);              break;
  case VMX_EXIT_REASON_EXECUTE_VMXON:                emulate_vmxon(cpu);               break;
  case VMX_EXIT_REASON_EXECUTE_VMCALL:               emulate_vmcall(cpu);              break;
  case VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED: handle_vmx_preemption(cpu);       break;
  case VMX_EXIT_REASON_EPT_VIOLATION:                handle_ept_violation(cpu);        break;
  case VMX_EXIT_REASON_EXECUTE_RDTSC:                emulate_rdtsc(cpu);               break;
  case VMX_EXIT_REASON_EXECUTE_RDTSCP:               emulate_rdtscp(cpu);              break;
  case VMX_EXIT_REASON_MONITOR_TRAP_FLAG:            handle_monitor_trap_flag(cpu);    break;
  case VMX_EXIT_REASON_EPT_MISCONFIGURATION:         handle_ept_misconfiguration(cpu); break;
  // VMX instructions (except for VMXON and VMCALL)
  case VMX_EXIT_REASON_EXECUTE_INVEPT:
  case VMX_EXIT_REASON_EXECUTE_INVVPID:
  case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
  case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
  case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
  case VMX_EXIT_REASON_EXECUTE_VMPTRST:
  case VMX_EXIT_REASON_EXECUTE_VMREAD:
  case VMX_EXIT_REASON_EXECUTE_VMRESUME:
  case VMX_EXIT_REASON_EXECUTE_VMWRITE:
  case VMX_EXIT_REASON_EXECUTE_VMXOFF:
  case VMX_EXIT_REASON_EXECUTE_VMFUNC:               handle_vmx_instruction(cpu);    break;

  // unhandled VM-exit
  default:
    HV_LOG_ERROR("Unhandled VM-exit. Exit Reason: %u. RIP: %p.",
      reason.basic_exit_reason, vmx_vmread(VMCS_GUEST_RIP));
    inject_hw_exception(general_protection, 0);
    break;
  }
}

// called for every vm-exit
bool handle_vm_exit(guest_context* const ctx) {
  // get the current vcpu
  auto const cpu = reinterpret_cast<vcpu*>(_readfsbase_u64());
  cpu->ctx = ctx;

  vmx_vmexit_reason reason;
  reason.flags = static_cast<uint32_t>(vmx_vmread(VMCS_EXIT_REASON));

  // dont hide tsc overhead by default
  cpu->hide_vm_exit_overhead = false;
  cpu->stop_virtualization   = false;

  dispatch_vm_exit(cpu, reason);

  vmentry_interrupt_information interrupt_info;
  interrupt_info.flags = static_cast<uint32_t>(
    vmx_vmread(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD));

  if (interrupt_info.valid) {
    char name[16] = {};
    current_guest_image_file_name(name);
    HV_LOG_INJECT_INT("Injecting interrupt into guest (%s). BasicExitReason=%i, Vector=%i, Error=%i.",
      name, reason.basic_exit_reason, interrupt_info.vector, vmx_vmread(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE));
  }

  // restore guest state. the assembly code is responsible for restoring
  // RIP, CS, RFLAGS, RSP, SS, CR0, CR4, as well as the usual fields in
  // the guest_context structure. the C++ code is responsible for the rest.
  if (cpu->stop_virtualization) {
    // TODO: assert that CPL is 0

    // ensure that the control register shadows reflect the guest values
    vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, read_effective_guest_cr0().flags);
    vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, read_effective_guest_cr4().flags);

    // DR7
    __writedr(7, vmx_vmread(VMCS_GUEST_DR7));

    // MSRs
    __writemsr(IA32_SYSENTER_CS,      vmx_vmread(VMCS_GUEST_SYSENTER_CS));
    __writemsr(IA32_SYSENTER_ESP,     vmx_vmread(VMCS_GUEST_SYSENTER_ESP));
    __writemsr(IA32_SYSENTER_EIP,     vmx_vmread(VMCS_GUEST_SYSENTER_EIP));
    __writemsr(IA32_PAT,              vmx_vmread(VMCS_GUEST_PAT));
    __writemsr(IA32_DEBUGCTL,         vmx_vmread(VMCS_GUEST_DEBUGCTL));
    __writemsr(IA32_PERF_GLOBAL_CTRL, cpu->msr_exit_store.perf_global_ctrl.msr_data);

    // CR3
    __writecr3(vmx_vmread(VMCS_GUEST_CR3));

    // GDT
    segment_descriptor_register_64 gdtr;
    gdtr.base_address = vmx_vmread(VMCS_GUEST_GDTR_BASE);
    gdtr.limit = static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_GDTR_LIMIT));
    _lgdt(&gdtr);

    // IDT
    segment_descriptor_register_64 idtr;
    idtr.base_address = vmx_vmread(VMCS_GUEST_IDTR_BASE);
    idtr.limit = static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_IDTR_LIMIT));
    __lidt(&idtr);

    segment_selector guest_tr;
    guest_tr.flags = static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_TR_SELECTOR));

    // TSS
    (reinterpret_cast<segment_descriptor_32*>(gdtr.base_address)
      + guest_tr.index)->type = SEGMENT_DESCRIPTOR_TYPE_TSS_AVAILABLE;
    write_tr(guest_tr.flags);

    // segment selectors
    write_ds(static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_DS_SELECTOR)));
    write_es(static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_ES_SELECTOR)));
    write_fs(static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_FS_SELECTOR)));
    write_gs(static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_GS_SELECTOR)));
    write_ldtr(static_cast<uint16_t>(vmx_vmread(VMCS_GUEST_LDTR_SELECTOR)));

    // FS and GS base address
    _writefsbase_u64(vmx_vmread(VMCS_GUEST_FS_BASE));
    _writegsbase_u64(vmx_vmread(VMCS_GUEST_GS_BASE));

    return true;
  }

  hide_vm_exit_overhead(cpu);

  // sync the vmcs state with the vcpu state
  vmx_vmwrite(VMCS_CTRL_TSC_OFFSET,                  cpu->tsc_offset);
  vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER_VALUE, cpu->preemption_timer);

  cpu->ctx = nullptr;

  return false;
}

// called for every host interrupt
void handle_host_interrupt(trap_frame* const frame) {
  switch (frame->vector) {
  // host NMIs
  case nmi: {
    auto ctrl = read_ctrl_proc_based();
    ctrl.nmi_window_exiting = 1;
    write_ctrl_proc_based(ctrl);

    auto const cpu = reinterpret_cast<vcpu*>(_readfsbase_u64());
    ++cpu->queued_nmis;

    break;
  }
  // host exceptions
  default: {
    // no registered exception handler
    if (!frame->r10 || !frame->r11) {
      HV_LOG_ERROR("Unhandled exception. RIP=hv.sys+%p. Vector=%u.",
        frame->rip - reinterpret_cast<UINT64>(&__ImageBase), frame->vector);

      // ensure a triple-fault
      segment_descriptor_register_64 idtr;
      idtr.base_address = frame->rsp;
      idtr.limit = 0xFFF;
      __lidt(&idtr);

      break;
    }

    HV_LOG_HOST_EXCEPTION("Handling host exception. RIP=hv.sys+%p. Vector=%u",
      frame->rip - reinterpret_cast<UINT64>(&__ImageBase), frame->vector);

    // jump to the exception handler
    frame->rip = frame->r10;

    auto const e = reinterpret_cast<host_exception_info*>(frame->r11);

    e->exception_occurred = true;
    e->vector             = frame->vector;
    e->error              = frame->error;

    // slightly helps prevent infinite exceptions
    frame->r10 = 0;
    frame->r11 = 0;
  }
  }
}

// virtualize the specified cpu. this assumes that execution is already
// restricted to the desired logical proocessor.
bool virtualize_cpu(vcpu* const cpu) {
  memset(cpu, 0, sizeof(*cpu));

  cache_cpu_data(cpu->cached);

  DbgPrint("[hv] Cached VCPU data.\n");

  if (!enable_vmx_operation(cpu)) {
    DbgPrint("[hv] Failed to enable VMX operation.\n");
    return false;
  }

  DbgPrint("[hv] Enabled VMX operation.\n");

  if (!enter_vmx_operation(cpu->vmxon)) {
    DbgPrint("[hv] Failed to enter VMX operation.\n");
    return false;
  }

  DbgPrint("[hv] Entered VMX operation.\n");

  if (!load_vmcs_pointer(cpu->vmcs)) {
    DbgPrint("[hv] Failed to load VMCS pointer.\n");
    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] Loaded VMCS pointer.\n");

  prepare_external_structures(cpu);

  DbgPrint("[hv] Initialized external structures.\n");

  write_vmcs_ctrl_fields(cpu);
  write_vmcs_host_fields(cpu);
  write_vmcs_guest_fields();

  DbgPrint("[hv] Wrote VMCS fields.\n");

  // TODO: should these fields really be set here? lol
  cpu->ctx                       = nullptr;
  cpu->queued_nmis               = 0;
  cpu->tsc_offset                = 0;
  cpu->preemption_timer          = 0;
  cpu->vm_exit_tsc_overhead      = 0;
  cpu->vm_exit_mperf_overhead    = 0;
  cpu->vm_exit_ref_tsc_overhead  = 0;

  DbgPrint("Launching VM on VCPU#%i...\n", KeGetCurrentProcessorIndex() + 1);

  if (!vm_launch()) {
    DbgPrint("[hv] VMLAUNCH failed. Instruction error = %lli.\n",
      vmx_vmread(VMCS_VM_INSTRUCTION_ERROR));

    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] Launched VM on VCPU#%i.\n", KeGetCurrentProcessorIndex() + 1);

  hypercall_input input;
  input.code = hypercall_ping;
  input.key  = hypercall_key;

  if (vmx_vmcall(input) == hypervisor_signature)
    DbgPrint("[hv] Successfully pinged the hypervisor.\n");

  cpu->vm_exit_tsc_overhead      = measure_vm_exit_tsc_overhead();
  cpu->vm_exit_mperf_overhead    = measure_vm_exit_mperf_overhead();
  cpu->vm_exit_ref_tsc_overhead  = measure_vm_exit_ref_tsc_overhead();

  DbgPrint("[hv] Measured VM-exit overhead (TSC = %zi).\n",
    cpu->vm_exit_tsc_overhead);
  DbgPrint("[hv] Measured VM-exit overhead (MPERF = %zi).\n",
    cpu->vm_exit_mperf_overhead);
  DbgPrint("[hv] Measured VM-exit overhead (CPU_CLK_UNHALTED.REF_TSC = %zi).\n",
    cpu->vm_exit_ref_tsc_overhead);

  return true;
}

} // namespace hv

