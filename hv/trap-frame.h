#pragma once

#include <ia32.hpp>

namespace hv {

struct trap_frame {
  // TODO: SSE registers...

  // general-purpose registers
  union {
    uint64_t rax;
    uint32_t eax;
    uint16_t ax;
    uint8_t  al;
  };
  union {
    uint64_t rcx;
    uint32_t ecx;
    uint16_t cx;
    uint8_t  cl;
  };
  union {
    uint64_t rdx;
    uint32_t edx;
    uint16_t dx;
    uint8_t  dl;
  };
  union {
    uint64_t rbx;
    uint32_t ebx;
    uint16_t bx;
    uint8_t  bl;
  };
  union {
    uint64_t rbp;
    uint32_t ebp;
    uint16_t bp;
    uint8_t  bpl;
  };
  union {
    uint64_t rsi;
    uint32_t esi;
    uint16_t si;
    uint8_t  sil;
  };
  union {
    uint64_t rdi;
    uint32_t edi;
    uint16_t di;
    uint8_t  dil;
  };
  union {
    uint64_t r8;
    uint32_t r8d;
    uint16_t r8w;
    uint8_t  r8b;
  };
  union {
    uint64_t r9;
    uint32_t r9d;
    uint16_t r9w;
    uint8_t  r9b;
  };
  union {
    uint64_t r10;
    uint32_t r10d;
    uint16_t r10w;
    uint8_t  r10b;
  };
  union {
    uint64_t r11;
    uint32_t r11d;
    uint16_t r11w;
    uint8_t  r11b;
  };
  union {
    uint64_t r12;
    uint32_t r12d;
    uint16_t r12w;
    uint8_t  r12b;
  };
  union {
    uint64_t r13;
    uint32_t r13d;
    uint16_t r13w;
    uint8_t  r13b;
  };
  union {
    uint64_t r14;
    uint32_t r14d;
    uint16_t r14w;
    uint8_t  r14b;
  };
  union {
    uint64_t r15;
    uint32_t r15d;
    uint16_t r15w;
    uint8_t  r15b;
  };

  // interrupt vector
  uint8_t vector;

  // _MACHINE_FRAME
  uint64_t error;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
};

// remember to update this value in interrupt-handlers.asm
static_assert(sizeof(trap_frame) == (0x78 + 0x38));

} // namespace hv

