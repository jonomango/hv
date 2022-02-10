#pragma once

namespace hv {

struct guest_context;

void emulate_cpuid(guest_context* ctx);

void emulate_rdmsr(guest_context* ctx);

void emulate_wrmsr(guest_context* ctx);

void emulate_mov_to_cr(guest_context* ctx);

void emulate_mov_from_cr(guest_context* ctx);

void emulate_clts(guest_context* ctx);

void emulate_lmsw(guest_context* ctx);

void handle_mov_cr(guest_context* ctx);

void handle_nmi_window(guest_context* ctx);

void handle_exception_or_nmi(guest_context* ctx);

} // namespace hv

