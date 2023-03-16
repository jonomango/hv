# hv

`hv` is an x86-64 [Intel VT-x](https://en.wikipedia.org/wiki/X86_virtualization#Intel_virtualization_(VT-x)) 
hypervisor that aims to be simple and lightweight, while still following the Intel SDM as closely as possible.
This allows it to evade detections that take advantage of common hypervisor bugs, such as improper
vm-exit handling. Other detections, such as timing checks, are ~~*mostly* mitigated~~, although staying
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
