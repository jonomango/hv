#include "idt.h"
#include "vcpu.h"
#include "interrupt-handlers.h"
#include "mm.h"

namespace hv {

// create an interrupt gate that points to the supplied interrupt handler
static segment_descriptor_interrupt_gate_64 create_interrupt_gate(void* const handler) {
  segment_descriptor_interrupt_gate_64 gate;

  gate.interrupt_stack_table      = 0;
  gate.segment_selector           = host_cs_selector.flags;
  gate.must_be_zero_0             = 0;
  gate.type                       = SEGMENT_DESCRIPTOR_TYPE_INTERRUPT_GATE;
  gate.must_be_zero_1             = 0;
  gate.descriptor_privilege_level = 0;
  gate.present                    = 1;
  gate.reserved                   = 0;

  auto const offset = reinterpret_cast<uint64_t>(handler);
  gate.offset_low    = (offset >> 0)  & 0xFFFF;
  gate.offset_middle = (offset >> 16) & 0xFFFF;
  gate.offset_high   = (offset >> 32) & 0xFFFFFFFF;

  return gate;
}

// initialize the host IDT and populate every descriptor
void prepare_host_idt(segment_descriptor_interrupt_gate_64* const idt) {
  memset(idt, 0, host_idt_descriptor_count * sizeof(idt[0]));
  idt[0]  = create_interrupt_gate(interrupt_handler_0);
  idt[1]  = create_interrupt_gate(interrupt_handler_1);
  idt[2]  = create_interrupt_gate(interrupt_handler_2);
  idt[3]  = create_interrupt_gate(interrupt_handler_3);
  idt[4]  = create_interrupt_gate(interrupt_handler_4);
  idt[5]  = create_interrupt_gate(interrupt_handler_5);
  idt[6]  = create_interrupt_gate(interrupt_handler_6);
  idt[7]  = create_interrupt_gate(interrupt_handler_7);
  idt[8]  = create_interrupt_gate(interrupt_handler_8);
  idt[10] = create_interrupt_gate(interrupt_handler_10);
  idt[11] = create_interrupt_gate(interrupt_handler_11);
  idt[12] = create_interrupt_gate(interrupt_handler_12);
  idt[13] = create_interrupt_gate(interrupt_handler_13);
  idt[14] = create_interrupt_gate(interrupt_handler_14);
  idt[16] = create_interrupt_gate(interrupt_handler_16);
  idt[17] = create_interrupt_gate(interrupt_handler_17);
  idt[18] = create_interrupt_gate(interrupt_handler_18);
  idt[19] = create_interrupt_gate(interrupt_handler_19);
  idt[20] = create_interrupt_gate(interrupt_handler_20);
  idt[30] = create_interrupt_gate(interrupt_handler_30);
}

} // namespace hv
