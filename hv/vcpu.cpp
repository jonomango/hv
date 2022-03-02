#include "vcpu.h"
#include "hv.h"
#include "gdt.h"
#include "idt.h"
#include "mm.h"
#include "vmx.h"
#include "arch.h"
#include "segment.h"
#include "trap-frame.h"
#include "exit-handlers.h"

namespace hv {

// defined in vm-launch.asm
bool vm_launch();

// defined in vm-exit.asm
void vm_exit();

// cache certain fixed values (CPUID results, MSRs, etc) that are used
// frequently during VMX operation (to speed up vm-exit handling).
static void cache_cpu_data(vcpu_cached_data& cached) {
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

  __cpuid(reinterpret_cast<int*>(&cached.cpuid_01), 0x01);
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

  auto vmxon_phys = get_physical(&vmxon_region);
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

  auto vmcs_phys = get_physical(&vmcs_region);
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

// initialize external structures that are not included in the VMCS
static void prepare_external_structures(vcpu* const cpu) {
  // setup the MSR bitmap so that we only exit on IA32_FEATURE_CONTROL
  memset(&cpu->msr_bitmap, 0, sizeof(cpu->msr_bitmap));
  enable_exiting_for_msr(cpu, IA32_FEATURE_CONTROL, true);

  // we don't care about anything that's in the TSS
  memset(&cpu->host_tss, 0, sizeof(cpu->host_tss));

  prepare_host_idt(cpu->host_idt);
  prepare_host_gdt(cpu->host_gdt, &cpu->host_tss);

  prepare_ept(cpu->ept);
}

// write to the control fields in the VMCS
static void write_vmcs_ctrl_fields(vcpu* const cpu) {
  // 3.26.2

  // 3.24.6.1
  ia32_vmx_pinbased_ctls_register pin_based_ctrl;
  pin_based_ctrl.flags                         = 0;
  pin_based_ctrl.virtual_nmi                   = 1;
  pin_based_ctrl.nmi_exiting                   = 1;
  pin_based_ctrl.activate_vmx_preemption_timer = 1;
  write_ctrl_pin_based_safe(pin_based_ctrl);

  // 3.24.6.2
  ia32_vmx_procbased_ctls_register proc_based_ctrl;
  proc_based_ctrl.flags                       = 0;
#ifndef NDEBUG
  proc_based_ctrl.cr3_load_exiting            = 1;
  proc_based_ctrl.cr3_store_exiting           = 1;
#endif
  proc_based_ctrl.use_msr_bitmaps             = 1;
  proc_based_ctrl.use_tsc_offsetting          = 1;
  proc_based_ctrl.activate_secondary_controls = 1;
  write_ctrl_proc_based_safe(proc_based_ctrl);

  // 3.24.6.2
  ia32_vmx_procbased_ctls2_register proc_based_ctrl2;
  proc_based_ctrl2.flags                  = 0;
  proc_based_ctrl2.enable_ept             = 1;
  proc_based_ctrl2.enable_rdtscp          = 1;
  proc_based_ctrl2.enable_vpid            = 1;
  proc_based_ctrl2.enable_invpcid         = 1;
  proc_based_ctrl2.enable_xsaves          = 1;
  proc_based_ctrl2.enable_user_wait_pause = 1;
  proc_based_ctrl2.conceal_vmx_from_pt    = 1;
  write_ctrl_proc_based2_safe(proc_based_ctrl2);

  // 3.24.7
  ia32_vmx_exit_ctls_register exit_ctrl;
  exit_ctrl.flags                   = 0;
  exit_ctrl.save_debug_controls     = 1;
  exit_ctrl.host_address_space_size = 1;
  exit_ctrl.save_ia32_pat           = 1;
  exit_ctrl.load_ia32_pat           = 1;
  exit_ctrl.conceal_vmx_from_pt     = 1;
  write_ctrl_exit_safe(exit_ctrl);

  // 3.24.8
  ia32_vmx_entry_ctls_register entry_ctrl;
  entry_ctrl.flags               = 0;
  entry_ctrl.load_debug_controls = 1;
  entry_ctrl.ia32e_mode_guest    = 1;
  entry_ctrl.load_ia32_pat       = 1;
  entry_ctrl.conceal_vmx_from_pt = 1;
  write_ctrl_entry_safe(entry_ctrl);

  // 3.24.6.3
  vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, 0);

  // set up the mask and match in such a way so
  // that a vm-exit is never triggered for a pagefault
  vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK,  0);
  vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, 0);
  
  // 3.24.6.5
  vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0);

  // 3.24.6.6
#ifdef NDEBUG
  // only vm-exit when guest tries to change a reserved bit
  vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK,
    cpu->cached.vmx_cr0_fixed0 | ~cpu->cached.vmx_cr0_fixed1);
  vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK,
    cpu->cached.vmx_cr4_fixed0 | ~cpu->cached.vmx_cr4_fixed1);
#else
  // vm-exit on every CR0/CR4 modification
  vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0xFFFFFFFF'FFFFFFFF);
  vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0xFFFFFFFF'FFFFFFFF);
#endif
  vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, __readcr0());
  vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, __readcr4() & ~CR4_VMX_ENABLE_FLAG);

  // 3.24.6.7
  // try to trigger the least amount of CR3 exits as possible
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_COUNT,   1);
  vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_0, ghv.system_cr3.flags);

  // 3.24.6.9
  vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, get_physical(&cpu->msr_bitmap));

  // 3.24.6.11
  ept_pointer eptp;
  eptp.flags                                = 0;
  eptp.memory_type                          = MEMORY_TYPE_WRITE_BACK;
  eptp.page_walk_length                     = 3;
  eptp.enable_access_and_dirty_flags        = 0;
  eptp.enable_supervisor_shadow_stack_pages = 0;
  eptp.page_frame_number                    = get_physical(&cpu->ept.pml4) >> 12;
  vmx_vmwrite(VMCS_CTRL_EPT_POINTER, eptp.flags);

  // 3.24.6.12
  vmx_vmwrite(VMCS_CTRL_VIRTUAL_PROCESSOR_IDENTIFIER, guest_vpid);

  // 3.24.7.2
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT,   0);
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_ADDRESS, 0);
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT,    0);
  vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_ADDRESS,  0);

  // 3.24.8.2
  vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT,   0);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_ADDRESS, 0);

  // 3.24.8.3
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE,           0);
  vmx_vmwrite(VMCS_CTRL_VMENTRY_INSTRUCTION_LENGTH,             0);
}

// write to the host fields in the VMCS
static void write_vmcs_host_fields(vcpu const* const cpu) {
  // 3.24.5
  // 3.26.2

  cr3 host_cr3;
  host_cr3.flags                     = 0;
  host_cr3.page_level_cache_disable  = 0;
  host_cr3.page_level_write_through  = 0;
  host_cr3.address_of_page_directory = get_physical(&ghv.host_page_tables.pml4) >> 12;
  vmx_vmwrite(VMCS_HOST_CR3, host_cr3.flags);

  cr4 host_cr4;
  host_cr4.flags = __readcr4();

  // these are flags that may or may not be set by Windows
  host_cr4.pcid_enable     = 0;
  host_cr4.fsgsbase_enable = 1;
  host_cr4.os_xsave        = 1;
  host_cr4.smap_enable     = 0;
  host_cr4.smep_enable     = 0;

  vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
  vmx_vmwrite(VMCS_HOST_CR4, host_cr4.flags);

  // ensure that rsp is NOT aligned to 16 bytes when execution starts
  auto const rsp = ((reinterpret_cast<size_t>(cpu->host_stack)
    + host_stack_size) & ~0b1111ull) - 8;

  vmx_vmwrite(VMCS_HOST_RSP, rsp);
  vmx_vmwrite(VMCS_HOST_RIP, reinterpret_cast<size_t>(vm_exit));

  vmx_vmwrite(VMCS_HOST_CS_SELECTOR, host_cs_selector.flags);
  vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0x00);
  vmx_vmwrite(VMCS_HOST_TR_SELECTOR, host_tr_selector.flags);

  vmx_vmwrite(VMCS_HOST_FS_BASE,   reinterpret_cast<size_t>(cpu));
  vmx_vmwrite(VMCS_HOST_GS_BASE,   0);
  vmx_vmwrite(VMCS_HOST_TR_BASE,   reinterpret_cast<size_t>(&cpu->host_tss));
  vmx_vmwrite(VMCS_HOST_GDTR_BASE, reinterpret_cast<size_t>(&cpu->host_gdt));
  vmx_vmwrite(VMCS_HOST_IDTR_BASE, reinterpret_cast<size_t>(&cpu->host_idt));

  vmx_vmwrite(VMCS_HOST_SYSENTER_CS,  0);
  vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, 0);
  vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, 0);

  ia32_pat_register host_pat;
  host_pat.flags = 0;

  // 3.11.12.4
  // configure PAT as if it wasn't supported (i.e. default settings after a reset)
  host_pat.pa0   = MEMORY_TYPE_WRITE_BACK;
  host_pat.pa1   = MEMORY_TYPE_WRITE_THROUGH;
  host_pat.pa2   = MEMORY_TYPE_UNCACHEABLE_MINUS;
  host_pat.pa3   = MEMORY_TYPE_UNCACHEABLE;
  host_pat.pa4   = MEMORY_TYPE_WRITE_BACK;
  host_pat.pa5   = MEMORY_TYPE_WRITE_THROUGH;
  host_pat.pa6   = MEMORY_TYPE_UNCACHEABLE_MINUS;
  host_pat.pa7   = MEMORY_TYPE_UNCACHEABLE;
  vmx_vmwrite(VMCS_HOST_PAT, host_pat.flags);
}

// write to the guest fields in the VMCS
static void write_vmcs_guest_fields() {
  // 3.24.4
  // 3.26.3

  vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());

  vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
  vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());

  vmx_vmwrite(VMCS_GUEST_DR7, __readdr(7));

  // RIP and RSP are set in vm-launch.asm
  vmx_vmwrite(VMCS_GUEST_RSP, 0);
  vmx_vmwrite(VMCS_GUEST_RIP, 0);

  vmx_vmwrite(VMCS_GUEST_RFLAGS, __readeflags());

  // TODO: don't hardcode the segment selectors idiot...

  vmx_vmwrite(VMCS_GUEST_CS_SELECTOR,   0x10);
  vmx_vmwrite(VMCS_GUEST_SS_SELECTOR,   0x18);
  vmx_vmwrite(VMCS_GUEST_DS_SELECTOR,   0x2B);
  vmx_vmwrite(VMCS_GUEST_ES_SELECTOR,   0x2B);
  vmx_vmwrite(VMCS_GUEST_FS_SELECTOR,   0x53);
  vmx_vmwrite(VMCS_GUEST_GS_SELECTOR,   0x2B);
  vmx_vmwrite(VMCS_GUEST_TR_SELECTOR,   0x40);
  vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, 0x00);

  segment_descriptor_register_64 gdtr, idtr;
  _sgdt(&gdtr);
  __sidt(&idtr);

  vmx_vmwrite(VMCS_GUEST_CS_BASE,   segment_base(gdtr, 0x10));
  vmx_vmwrite(VMCS_GUEST_SS_BASE,   segment_base(gdtr, 0x18));
  vmx_vmwrite(VMCS_GUEST_DS_BASE,   segment_base(gdtr, 0x2B));
  vmx_vmwrite(VMCS_GUEST_ES_BASE,   segment_base(gdtr, 0x2B));
  vmx_vmwrite(VMCS_GUEST_FS_BASE,   __readmsr(IA32_FS_BASE));
  vmx_vmwrite(VMCS_GUEST_GS_BASE,   __readmsr(IA32_GS_BASE));
  vmx_vmwrite(VMCS_GUEST_TR_BASE,   segment_base(gdtr, 0x40));
  vmx_vmwrite(VMCS_GUEST_LDTR_BASE, segment_base(gdtr, 0x00));

  vmx_vmwrite(VMCS_GUEST_CS_LIMIT,   __segmentlimit(0x10));
  vmx_vmwrite(VMCS_GUEST_SS_LIMIT,   __segmentlimit(0x18));
  vmx_vmwrite(VMCS_GUEST_DS_LIMIT,   __segmentlimit(0x2B));
  vmx_vmwrite(VMCS_GUEST_ES_LIMIT,   __segmentlimit(0x2B));
  vmx_vmwrite(VMCS_GUEST_FS_LIMIT,   __segmentlimit(0x53));
  vmx_vmwrite(VMCS_GUEST_GS_LIMIT,   __segmentlimit(0x2B));
  vmx_vmwrite(VMCS_GUEST_TR_LIMIT,   __segmentlimit(0x40));
  vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, __segmentlimit(0x00));

  vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS,   segment_access(gdtr, 0x10).flags);
  vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS,   segment_access(gdtr, 0x18).flags);
  vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS,   segment_access(gdtr, 0x2B).flags);
  vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS,   segment_access(gdtr, 0x2B).flags);
  vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS,   segment_access(gdtr, 0x53).flags);
  vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS,   segment_access(gdtr, 0x2B).flags);
  vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS,   segment_access(gdtr, 0x40).flags);
  vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, segment_access(gdtr, 0x00).flags);

  vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdtr.base_address);
  vmx_vmwrite(VMCS_GUEST_IDTR_BASE, idtr.base_address);

  vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr.limit);
  vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr.limit);

  vmx_vmwrite(VMCS_GUEST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
  vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
  vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
  vmx_vmwrite(VMCS_GUEST_DEBUGCTL,     __readmsr(IA32_DEBUGCTL));
  vmx_vmwrite(VMCS_GUEST_PAT,          __readmsr(IA32_PAT));

  vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, vmx_active);

  vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);

  vmx_vmwrite(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

  vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, MAXULONG64);

  vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER_VALUE, MAXULONG64);
}

// measure the amount of overhead that a vm-exit
// causes so that we can use TSC offsetting to hide it
static uint64_t measure_vm_exit_tsc_latency() {
  uint64_t lowest_latency = MAXULONG64;

  // we dont want to be interrupted (NMIs and SMIs can fuck off)
  _disable();

  // measure the execution time of a vm-entry and vm-exit
  for (int i = 0; i < 10; ++i) {
    // first measure the overhead of rdtsc/rdtsc

    _mm_lfence();
    auto start = __rdtsc();
    _mm_lfence();

    _mm_lfence();
    auto end = __rdtsc();
    _mm_lfence();

    auto const rdtsc_overhead = end - start;

    // next, measure the overhead of a vm-exit

    _mm_lfence();
    start = __rdtsc();
    _mm_lfence();

    vmx_vmcall(hypercall_ping);

    _mm_lfence();
    end = __rdtsc();
    _mm_lfence();

    // this is the time it takes, in TSC, for a vm-exit that does no handling
    auto const curr = (end - start) - rdtsc_overhead;

    if (curr < lowest_latency)
      lowest_latency = curr;
  }

  _enable();

  // return the lowest execution time as the vm-exit latency
  return lowest_latency;
}

// using TSC offsetting to hide vm-exit TSC latency
static void hide_vm_exit_tsc_latency(vcpu* const cpu) {
  if (cpu->hide_vm_exit_latency) {
    // hotfix to prevent TSC offsetting while running in a nested HV
    if (cpu->vm_exit_tsc_latency > 10000)
      return;

    cpu->tsc_offset -= cpu->vm_exit_tsc_latency;

    // set preemption timer to cause an exit after 10000 guest TSC ticks have passed
    cpu->preemption_timer = max(2,
      10000 >> cpu->cached.vmx_misc.preemption_timer_tsc_relationship);
  } else {
    // reset TSC offset to 0 during vm-exits that are not likely to be timed
    cpu->tsc_offset = 0;

    // soft disable the VMX preemption timer
    cpu->preemption_timer = MAXULONG64;
  }
}

// call the appropriate exit-handler for this vm-exit
static void dispatch_vm_exit(vcpu* const cpu, vmx_vmexit_reason const reason) {
  switch (reason.basic_exit_reason) {
  case VMX_EXIT_REASON_EXCEPTION_OR_NMI:             handle_exception_or_nmi(cpu); break;
  case VMX_EXIT_REASON_EXECUTE_GETSEC:               emulate_getsec(cpu);          break;
  case VMX_EXIT_REASON_EXECUTE_INVD:                 emulate_invd(cpu);            break;
  case VMX_EXIT_REASON_NMI_WINDOW:                   handle_nmi_window(cpu);       break;
  case VMX_EXIT_REASON_EXECUTE_CPUID:                emulate_cpuid(cpu);           break;
  case VMX_EXIT_REASON_MOV_CR:                       handle_mov_cr(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_RDMSR:                emulate_rdmsr(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_WRMSR:                emulate_wrmsr(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_XSETBV:               emulate_xsetbv(cpu);          break;
  case VMX_EXIT_REASON_EXECUTE_VMXON:                emulate_vmxon(cpu);           break;
  case VMX_EXIT_REASON_EXECUTE_VMCALL:               emulate_vmcall(cpu);          break;
  case VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED: handle_vmx_preemption(cpu);   break;
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
  case VMX_EXIT_REASON_EXECUTE_VMFUNC:               handle_vmx_instruction(cpu);  break;
  }
}

// called for every vm-exit
void handle_vm_exit(guest_context* const ctx) {
  // get the current vcpu
  auto const cpu = reinterpret_cast<vcpu*>(_readfsbase_u64());
  cpu->ctx = ctx;

  vmx_vmexit_reason reason;
  reason.flags = static_cast<uint32_t>(vmx_vmread(VMCS_EXIT_REASON));

  // dont hide tsc latency by default
  cpu->hide_vm_exit_latency = false;

  // handle the vm-exit
  dispatch_vm_exit(cpu, reason);

  // hide the vm-exit overhead from the guest
  hide_vm_exit_tsc_latency(cpu);

  // update the TSC offset and VMX preemption timer
  vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, cpu->tsc_offset);
  vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER_VALUE, cpu->preemption_timer);

  vmentry_interrupt_information vectoring_info;
  vectoring_info.flags = static_cast<uint32_t>(vmx_vmread(VMCS_IDT_VECTORING_INFORMATION));

  // 3.27.2.4
  // TODO: vm-exit during event delivery
  if (vectoring_info.valid) {

  }

  cpu->ctx = nullptr;
}

// called for every host interrupt
void handle_host_interrupt(trap_frame* const frame) {
  switch (frame->vector) {
  // host NMIs
  case nmi:
    auto ctrl = read_ctrl_proc_based();
    ctrl.nmi_window_exiting = 1;
    write_ctrl_proc_based(ctrl);
    break;
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
  cpu->tsc_offset          = 0;
  cpu->preemption_timer    = 0;
  cpu->vm_exit_tsc_latency = 0;

  if (!vm_launch()) {
    DbgPrint("[hv] VMLAUNCH failed. Instruction error = %lli.\n", vmx_vmread(VMCS_VM_INSTRUCTION_ERROR));
    vmx_vmxoff();
    return false;
  }

  DbgPrint("[hv] Launched virtual machine on VCPU#%i.\n",
    KeGetCurrentProcessorIndex() + 1);

  cpu->vm_exit_tsc_latency = measure_vm_exit_tsc_latency();

  DbgPrint("[hv] Measured VM-exit TSC latency (%zi).\n",
    cpu->vm_exit_tsc_latency);

  return true;
}

// toggle vm-exiting for the specified MSR through the MSR bitmap
void enable_exiting_for_msr(vcpu* const cpu, uint32_t msr, bool const enabled) {
  auto const bit = static_cast<uint8_t>(enabled ? 1 : 0);

  if (msr <= MSR_ID_LOW_MAX) {
    // set the bit in the low bitmap
    cpu->msr_bitmap.rdmsr_low[msr / 8] = (bit << (msr & 0b0111));
    cpu->msr_bitmap.wrmsr_low[msr / 8] = (bit << (msr & 0b0111));
  } else if (msr >= MSR_ID_HIGH_MIN && msr <= MSR_ID_HIGH_MAX) {
    msr -= MSR_ID_HIGH_MIN;

    // set the bit in the high bitmap
    cpu->msr_bitmap.rdmsr_high[msr / 8] = (bit << (msr & 0b0111));
    cpu->msr_bitmap.wrmsr_high[msr / 8] = (bit << (msr & 0b0111));
  }
}

} // namespace hv
