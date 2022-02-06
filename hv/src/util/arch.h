#pragma once

#include <intrin.h>
#include <ia32.hpp>

extern "C" {

// https://docs.microsoft.com/en-us/cpp/intrinsics/x64-amd64-intrinsics-list
void _sgdt(segment_descriptor_register_64* gdtr);
void _lgdt(segment_descriptor_register_64* gdtr);

} // extern "C"

namespace hv {

// it's definately not safe to name these functions
// with a double underscore prefix but... whatever.

void __vmx_invept(invept_type type, invept_descriptor const& descriptor);

// helper function for non-cancer syntax
inline size_t __vmx_vmread(size_t const field) {
  size_t value;
  ::__vmx_vmread(field, &value);
  return value;
}

} // namespace hv
