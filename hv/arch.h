#pragma once

#include <intrin.h>
#include <ia32.hpp>

extern "C" {

// https://docs.microsoft.com/en-us/cpp/intrinsics/x64-amd64-intrinsics-list
void _sgdt(segment_descriptor_register_64* gdtr);
void _lgdt(segment_descriptor_register_64* gdtr);

} // extern "C"

