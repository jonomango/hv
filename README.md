# hv

`hv` is an x86-64 [Intel VT-x](https://en.wikipedia.org/wiki/X86_virtualization#Intel_virtualization_(VT-x)) 
hypervisor that aims to be simple and lightweight, while still following the Intel SDM as closely as possible.
This allows it to evade detections that take advantage of common hypervisor bugs, such as improper
vm-exit handling. Other detections, such as timing checks, are *mostly* mitigated, although staying
fully undetected is nearly impossible.

## Installation

To clone the repo:

```powershell
git clone --recursive https://github.com/jonomango/hv.git
```

`hv` is a Windows driver built with MSVC. It requires 
[Visual Studio](https://visualstudio.microsoft.com/downloads/) and the
[WDK](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk) for compilation.

Once compiled, simply load `hv.sys` anyway you'd like (either by using the Windows Service Control Manager
or a manual mapper) and it will immediately virtualize the system. To check if `hv` is
currently running, try to execute the ping hypercall and see if it responds appropriately. Unloading
the driver will result in `hv::stop()` being called, which will devirtualize the system.

## Hypercalls

`hv` has a full hypercall interface that can be used from both ring-0 and ring-3. It relies on the `VMCALL`
instruction and has a particular calling convention that must be followed. Check out
[um/hv.asm](https://github.com/jonomango/hv/blob/main/um/hv.asm) for how this can be implemented.

Additionally, hypercalls will not function correctly unless provided with the correct
[hypercall_key](https://github.com/jonomango/hv/blob/main/hv/hypercalls.h#L11). This can be easily modified
in the source code and is used to ensure that malicious guests cannot execute any hypercalls.

Example of executing the `ping` hypercall:

```cpp
// setup the hypercall input
hv::hypercall_input input;
input.code = hv::hypercall_ping;
input.key  = hv::hypercall_key;

// execute the hypercall
auto const value = hv::vmx_vmcall(input);

if (value == hv::hypervisor_signature)
  printf("pong!\n");
```

### Adding New Hypercalls

Extending the hypercall interface is pretty simple. Add your new hypercall handler to
[hv/hypercalls.h](https://github.com/jonomango/hv/blob/main/hv/hypercalls.h) and
[hv/hypercalls.cpp](https://github.com/jonomango/hv/blob/main/hv/hypercalls.cpp), then modify
[emulate_vmcall()](https://github.com/jonomango/hv/blob/main/hv/exit-handlers.cpp#L193-L204) to call
your added function.

## Important Root-Mode Functions

Below is a list of important functions that can be safely called from root-mode,
which can be used by including `introspection.h`. It is important to note that
any code that lies outside of the hypervisor (i.e. kernel code) cannot be called
from root-mode, and the following functions should be used instead.

```cpp
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

// get the CPL (current privilege level) of the current guest
uint16_t current_guest_cpl();

// attempt to read the memory at the specified guest virtual address from root-mode
size_t read_guest_virtual_memory(cr3 guest_cr3, void* gva, void* buffer, size_t size);

// attempt to read the memory at the specified guest virtual address from root-mode
size_t read_guest_virtual_memory(void* gva, void* buffer, size_t size);

// attempt to read the memory at the specified guest physical address from root-mode
bool read_guest_physical_memory(uint64_t gpa, void* buffer, size_t size);
```

## Logging

`hv` has a simple printf-style logger that can be used from both root-mode or
guest-mode. It can be found in `logger.h` and the logs can be retrieved through
the `flush_logs` hypercall. Different log types can be omitted by simply modifying
the defines in `logger.h`.

```cpp
// 3 different log types are supported:
HV_LOG_INFO("Hello world!");
HV_LOG_ERROR("Error on line %i.", 69);
HV_LOG_VERBOSE("Junk... %s", "More junk...");
```

The logger supports a small subset of printf-style formatting:

```
%s             c-style strings
%i or %d       32-bit signed integer
%u             32-bit unsigned integer
%x             32-bit unsigned integer (printed in hex, lowercase)
%X             32-bit unsigned integer (printed in hex, uppercase)
%p             64-bit unsigned integer (printed in hex)
```

Below is an example of reading the logs, which can be done from ring-0 or ring-3.

```cpp
// read the logs into a buffer
uint32_t count = 512;
hv::logger_msg msgs[512];
hv::flush_logs(count, msgs);

// print every log message
for (uint32_t i = 0; i < count; ++i)
  printf("%s\n", msgs[i].data);
```
