#pragma once

#include <cstdint>

namespace hv {

// key used for executing hypercalls
inline constexpr uint64_t hypercall_key = 69420;

// signature that is returned by the ping hypercall
inline constexpr uint64_t hypervisor_signature = 'fr0g';

struct logger_msg {
  static constexpr uint32_t max_msg_length = 128;

  // ID of the current message
  uint64_t id;

  // timestamp counter of the current message
  uint64_t tsc;

  // null-terminated ascii string
  char data[max_msg_length];
};

// hypercall indices
enum hypercall_code : uint64_t {
  hypercall_ping = 0,
  hypercall_test,
  hypercall_unload,
  hypercall_read_phys_mem,
  hypercall_write_phys_mem,
  hypercall_read_virt_mem,
  hypercall_write_virt_mem,
  hypercall_query_process_cr3,
  hypercall_install_ept_hook,
  hypercall_remove_ept_hook,
  hypercall_flush_logs,
  hypercall_get_physical_address
};

// hypercall input
struct hypercall_input {
  // rax
  struct {
    hypercall_code code : 8;
    uint64_t       key  : 56;
  };

  // rcx, rdx, r8, r9, r10, r11
  uint64_t args[6];
};

// VMCALL instruction, defined in hv.asm
uint64_t vmx_vmcall(hypercall_input& input);

inline uint64_t ping() {
  hv::hypercall_input input;
  input.code = hv::hypercall_ping;
  input.key  = hv::hypercall_key;
  return hv::vmx_vmcall(input);
}

inline uint64_t test(uint64_t const a1 = 0, uint64_t const a2 = 0,
                     uint64_t const a3 = 0, uint64_t const a4 = 0,
                     uint64_t const a5 = 0, uint64_t const a6 = 0) {
  hv::hypercall_input input;
  input.code = hv::hypercall_test;
  input.key  = hv::hypercall_key;
  input.args[0] = a1;
  input.args[1] = a2;
  input.args[2] = a3;
  input.args[3] = a4;
  input.args[4] = a5;
  input.args[5] = a6;
  return hv::vmx_vmcall(input);
}

inline size_t read_phys_mem(void* const dst, uint64_t const src,
                            size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_read_phys_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = reinterpret_cast<uint64_t>(dst);
  input.args[1] = src;
  input.args[2] = size;
  return hv::vmx_vmcall(input);
}

inline size_t write_phys_mem(uint64_t const dst, void const* const src,
                             size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_write_phys_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = dst;
  input.args[1] = reinterpret_cast<uint64_t>(src);
  input.args[2] = size;
  return hv::vmx_vmcall(input);
}

inline size_t read_virt_mem(uint64_t const cr3, void* const dst,
                            void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_read_virt_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3;
  input.args[1] = reinterpret_cast<uint64_t>(dst);
  input.args[2] = reinterpret_cast<uint64_t>(src);
  input.args[3] = size;
  return hv::vmx_vmcall(input);
}

inline size_t write_virt_mem(uint64_t const cr3, void* const dst,
                             void const* const src, size_t const size) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_write_virt_mem;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3;
  input.args[1] = reinterpret_cast<uint64_t>(dst);
  input.args[2] = reinterpret_cast<uint64_t>(src);
  input.args[3] = size;
  return hv::vmx_vmcall(input);
}

inline uint64_t query_process_cr3(uint64_t const pid) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_query_process_cr3;
  input.key     = hv::hypercall_key;
  input.args[0] = pid;
  return hv::vmx_vmcall(input);
}

inline void flush_logs(uint32_t& count, logger_msg* msgs) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_flush_logs;
  input.key     = hv::hypercall_key;
  input.args[0] = count;
  input.args[1] = reinterpret_cast<uint64_t>(msgs);
  count = static_cast<uint32_t>(hv::vmx_vmcall(input));
}

inline uint64_t get_physical_address(uint64_t const cr3, void const* const address) {
  hv::hypercall_input input;
  input.code    = hv::hypercall_get_physical_address;
  input.key     = hv::hypercall_key;
  input.args[0] = cr3;
  input.args[1] = reinterpret_cast<uint64_t>(address);
  return hv::vmx_vmcall(input);
}

} // namespace hv

