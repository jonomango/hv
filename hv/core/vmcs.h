#pragma once

#include <ia32.hpp>

namespace hv {

// write to the pin-based vm-execution controls
void write_ctrl_pin_based(ia32_vmx_pinbased_ctls_register value);

// write to the processor-based vm-execution controls
void write_ctrl_proc_based(ia32_vmx_procbased_ctls_register value);

// write to the secondary processor-based vm-execution controls
void write_ctrl_proc_based2(ia32_vmx_procbased_ctls2_register value);

// write to the vm-exit controls
void write_ctrl_exit(ia32_vmx_exit_ctls_register value);

// write to the vm-entry controls
void write_ctrl_entry(ia32_vmx_entry_ctls_register value);

} // namespace hv

