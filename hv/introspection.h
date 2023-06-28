#pragma once

#include "vmx.h"
#include "mm.h"

#include <ntddk.h>

namespace hv {

// get the KPCR of the current guest (this pointer should stay constant per-vcpu)
PKPCR current_guest_kpcr();

// get the ETHREAD of the current guest
PETHREAD current_guest_ethread();

// get the EPROCESS of the current guest
PEPROCESS current_guest_eprocess();

// get the PID of the current guest
uint64_t current_guest_pid();

// get the kernel CR3 of the current guest
cr3 current_guest_cr3();

// get the image file name (up to 15 chars) of the current guest process
bool current_guest_image_file_name(char (&name)[16]);

} // namespace hv

