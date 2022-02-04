#pragma once

#include <intrin.h>
#include <ia32.hpp>

extern "C" {

// https://docs.microsoft.com/en-us/cpp/intrinsics/x64-amd64-intrinsics-list
void _sgdt(int*);
void _lgdt(void*);

} // extern "C"

namespace hv {

//
// definately not safe to name these with a double underscore prefix but... whatever.
//

void __vmx_invept(invept_type type, invept_descriptor const& descriptor);

// helper function for non-cancer syntax
inline size_t __vmx_vmread(size_t const field) {
  size_t value;
  ::__vmx_vmread(field, &value);
  return value;
}

} // namespace hv
