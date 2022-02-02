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
  bool check_capabilities() const;

private:
  alignas(0x1000) vmxon vmxon_;
};

} // namespace hv

