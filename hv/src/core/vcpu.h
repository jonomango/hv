#pragma once

#include <ia32.hpp>

namespace hv {

class vcpu {
public:
  // virtualize the current cpu
  // note, this assumes that execution is already restricted to the desired cpu
  bool virtualize();

private:
  // check if VMX operation is supported
  bool check_vmx_capabilities() const;

  // perform certain actions that are required before entering vmx operation
  void enable_vmx_operation();

private:
  // 4 KiB vmxon region
  alignas(0x1000) vmxon vmxon_;

  // 4 KiB vmcs region
  alignas(0x1000) vmcs vmcs_;
};

} // namespace hv

