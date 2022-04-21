#include "hv.h"

#include <ntddk.h>
#include <ia32.hpp>

uint8_t* find_bytepatch_address() {
  // ZwClose      48 8B C4                 mov     rax, rsp
  // ZwClose+3    FA                       cli
  // ZwClose+4    48 83 EC 10              sub     rsp, 10h
  // ZwClose+8    50                       push    rax
  // ZwClose+9    9C                       pushfq
  // ZwClose+A    6A 10                    push    10h
  // ZwClose+C    48 8D 05 3D 6B 00 00     lea     rax, KiServiceLinkage
  // ZwClose+13   50                       push    rax
  // ZwClose+14   B8 0F 00 00 00           mov     eax, 0Fh
  // ZwClose+19   E9 42 24 01 00           jmp     KiServiceInternal
  // ZwClose+1E   C3                       retn
  auto const zw_close = reinterpret_cast<uint8_t*>(ZwClose);

  // sanity check
  if (zw_close[0x19] != 0xE9)
    return nullptr;

  // address of KiServiceInternal
  auto const ki_service_internal = zw_close + 0x1E
    + *reinterpret_cast<int*>(zw_close + 0x1A);

  // KiSystemCall64 is always directly after KiServiceInternal.
  // search for the instruction that increment KPRCB->KeSystemCalls, since
  // this is where we'll by bytepatching.
  for (size_t offset = 0; offset < 0x1000; ++offset) {
    auto const curr = ki_service_internal + offset;

    // KiSystemCall64+453  65 FF 04 25 38 2E 00 00      inc     dword ptr gs:2E38h
    // KiSystemCall64+45B  48 8B 9D C0 00 00 00         mov     rbx, [rbp+0C0h]
    // KiSystemCall64+462  48 8B BD C8 00 00 00         mov     rdi, [rbp+0C8h]
    // KiSystemCall64+469  48 8B B5 D0 00 00 00         mov     rsi, [rbp+0D0h]
    // 
    // this is a pretty unique instruction (the inc gs), so we
    // shouldn't have to worry too much about getting any false positives.
    if (*reinterpret_cast<uint32_t*>(curr) == 0x2504FF65) {
      // sanity check
      if (*reinterpret_cast<uint32_t*>(curr + 8)  != 0xC09D8B48 ||
          *reinterpret_cast<uint32_t*>(curr + 15) != 0xC8BD8B48 ||
          *reinterpret_cast<uint32_t*>(curr + 22) != 0xD0B58B48)
        return nullptr;

      // we only care about the first two mov instructions
      return curr + 8;
    }
  }

  return nullptr;
}

extern "C" void syscall_hook_trampoline();

static uint8_t* g_bytepatch_addr;
static uint8_t g_orig_bytes[14];

extern "C" void syscall_hook(KTRAP_FRAME* const) {
  // NtClose
  //if (frame->Rax == 0xF)
    //__debugbreak();
}

void install_hook() {
  g_bytepatch_addr = find_bytepatch_address();
  DbgPrint("Bytepatch address: 0x%p.\n", g_bytepatch_addr);

  // copy the original bytes so we can restore them later
  memcpy(g_orig_bytes, g_bytepatch_addr, sizeof(g_orig_bytes));

  uint8_t new_bytes[14] = {
    0x48, 0xBB, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
    0xFF, 0xD3,
    0x90,
    0x90
  };

  *reinterpret_cast<void**>(new_bytes + 2) = syscall_hook_trampoline;

  // install our hook
  memcpy(g_bytepatch_addr, new_bytes, sizeof(new_bytes));
}

void remove_hook() {
  // restore original bytes
  memcpy(g_bytepatch_addr, g_orig_bytes, sizeof(g_orig_bytes));
}

void driver_unload(PDRIVER_OBJECT) {
  DbgPrint("[hv] Driver unloaded.\n");

  //remove_hook();
}

NTSTATUS driver_entry(PDRIVER_OBJECT const driver, PUNICODE_STRING) {
  if (driver)
    driver->DriverUnload = driver_unload;

  DbgPrint("[hv] Driver loaded.\n");

  if (!hv::start()) {
    DbgPrint("[hv] Failed to virtualize system.\n");
    return STATUS_HV_OPERATION_FAILED;
  }

  g_bytepatch_addr = find_bytepatch_address();
  DbgPrint("Bytepatch address: 0x%p.\n", g_bytepatch_addr);

  // copy the original bytes so we can restore them later
  memcpy(g_orig_bytes, g_bytepatch_addr, sizeof(g_orig_bytes));

  uint8_t new_bytes[14] = {
    0x48, 0xBB, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
    0xFF, 0xD3,
    0x90,
    0x90
  };

  *reinterpret_cast<void**>(new_bytes + 2) = syscall_hook_trampoline;

  auto const exec_page = (uint8_t*)ExAllocatePoolWithTag(NonPagedPool, 0x1000, 'pepe');
  memcpy(exec_page, reinterpret_cast<void*>(reinterpret_cast<uint64_t>(g_bytepatch_addr) & ~0xFFFull), 0x1000);

  // install our hook
  memcpy(exec_page + ((uint64_t)g_bytepatch_addr & 0xFFF), new_bytes, sizeof(new_bytes));

  for (size_t i = 0; i < KeQueryActiveProcessorCount(nullptr); ++i) {
    auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << i);

    hv::hypercall_input input;
    input.code = hv::hypercall_test;
    input.key  = hv::hypercall_key;
    input.args[0] = MmGetPhysicalAddress(g_bytepatch_addr).QuadPart;
    input.args[1] = MmGetPhysicalAddress(exec_page).QuadPart;

    hv::vmx_vmcall(input);

    KeRevertToUserAffinityThreadEx(orig_affinity);
  }

  for (int i = 0; i < 14; ++i)
    DbgPrint("%.2X ", g_bytepatch_addr[i]);

  //install_hook();

  return STATUS_SUCCESS;
}

