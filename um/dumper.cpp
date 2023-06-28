#include "dumper.h"
#include "hv.h"

#include <fstream>

struct RTL_PROCESS_MODULE_INFORMATION {
  PVOID  Section;
  PVOID  MappedBase;
  PVOID  ImageBase;
  ULONG  ImageSize;
  ULONG  Flags;
  USHORT LoadOrderIndex;
  USHORT InitOrderIdnex;
  USHORT LoadCount;
  USHORT OffsetToFileName;
  CHAR   FullPathName[0x100];
};

struct RTL_PROCESS_MODULES {
  ULONG                          NumberOfModules;
  RTL_PROCESS_MODULE_INFORMATION Modules[1];
};

// get the image base and image size of a loaded driver
bool find_loaded_driver(char const* const name, void*& imagebase, uint32_t& imagesize) {
  using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(uint32_t SystemInformationClass,
    PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
  static auto const NtQuerySystemInformation = (NtQuerySystemInformationFn)(
    GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation"));

  // get the size of the buffer that we need to allocate
  unsigned long length = 0;
  NtQuerySystemInformation(0x0B, nullptr, 0, &length);

  auto const info = (RTL_PROCESS_MODULES*)(new uint8_t[length + 0x200]);
  NtQuerySystemInformation(0x0B, info, length + 0x200, &length);

  for (unsigned int i = 0; i < info->NumberOfModules; ++i) {
    auto const& m = info->Modules[i];
    if (strcmp(m.FullPathName + m.OffsetToFileName, name) != 0)
      continue;

    imagebase = m.ImageBase;
    imagesize = m.ImageSize;

    delete info;
    return true;
  }

  delete info;
  return false;
}

// dump a running driver to a file
bool dump_driver(char const* const name, char const* path) {
  if (!hv::is_hv_running())
    return false;

  void*    imagebase = nullptr;
  uint32_t imagesize = 0;

  if (!find_loaded_driver(name, imagebase, imagesize))
    return false;

  auto const buffer = std::make_unique<uint8_t[]>(imagesize);
  if (imagesize != hv::read_virt_mem(0, buffer.get(), imagebase, imagesize))
    return false;

  auto const dos_header = (PIMAGE_DOS_HEADER)&buffer[0];
  auto const nt_header = (PIMAGE_NT_HEADERS)(&buffer[0] + dos_header->e_lfanew);
  auto const sections = (PIMAGE_SECTION_HEADER)(nt_header + 1);

  // fix the imagebase field in the PE header
  nt_header->OptionalHeader.ImageBase = (uintptr_t)imagebase;

  // fix the sections
  for (size_t i = 0; i < nt_header->FileHeader.NumberOfSections; ++i)
    sections[i].PointerToRawData = sections[i].VirtualAddress;

  char file_name[1024] = {};

  if (!path) {
    sprintf_s(file_name, "%s.dump", name);
    path = file_name;
  }

  std::ofstream file(path, std::ios::binary);
  file.write((char const*)buffer.get(), imagesize);

  return true;
}

