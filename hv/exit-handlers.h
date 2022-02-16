#pragma once

#include <ia32.hpp>

namespace hv {

class vcpu;

void emulate_cpuid(vcpu* cpu);

void emulate_rdmsr(vcpu* cpu);

void emulate_wrmsr(vcpu* cpu);

void emulate_getsec(vcpu* cpu);

void emulate_invd(vcpu* cpu);

void emulate_xsetbv(vcpu* cpu);

void emulate_vmxon(vcpu* cpu);

void emulate_vmcall(vcpu* cpu);

void emulate_mov_to_cr0(vcpu* cpu);

void emulate_mov_to_cr3(vcpu* cpu);

void emulate_mov_to_cr4(vcpu* cpu);

void emulate_mov_from_cr3(vcpu* cpu);

void emulate_clts(vcpu* cpu);

void emulate_lmsw(vcpu* cpu);

void handle_mov_cr(vcpu* cpu);

void handle_nmi_window(vcpu* cpu);

void handle_exception_or_nmi(vcpu* cpu);

} // namespace hv

