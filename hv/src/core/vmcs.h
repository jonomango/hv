#pragma once

#include <ia32.hpp>

namespace hv {

// prepare the vmcs before launching the virtual machine
bool prepare_vmcs(vmcs& vmcs);

} // namespace hv

