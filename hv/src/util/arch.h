#pragma once

#include <intrin.h>
#include <ia32.hpp>

namespace hv {

void __invept(invept_type type, invept_descriptor const& descriptor);

// helper function for non-cancer syntax
inline uint64_t __vmx_vmread(size_t const field) {
  size_t value;
  ::__vmx_vmread(field, &value);
  return value;
}

} // namespace hv
