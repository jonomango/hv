#include "idt.h"
#include "gdt.h"
#include "interrupt-handlers.h"

#include "../util/mm.h"

namespace hv {

// create an interrupt gate that points to the supplied interrupt handler
static interrupt_gate_descriptor_64 create_interrupt_gate(void* const handler) {
  interrupt_gate_descriptor_64 gate;
  gate.flags_low                  = 0;
  gate.flags_high                 = 0;
  gate.segment_selector           = host_cs_selector.flags;
  gate.interrupt_stack_table      = 0;
  gate.type                       = SEGMENT_DESCRIPTOR_TYPE_INTERRUPT_GATE;
  gate.descriptor_privilege_level = 0;
  gate.present                    = 1;
  gate.offset_low    = (reinterpret_cast<uint64_t>(handler) >> 0)  & 0xFFFF;
  gate.offset_middle = (reinterpret_cast<uint64_t>(handler) >> 16) & 0xFFFF;
  gate.offset_high   = (reinterpret_cast<uint64_t>(handler) >> 32) & 0xFFFFFFFF;

  return gate;
}

// initialize the host IDT and populate every descriptor
void prepare_host_idt(host_idt& idt) {
  memset(idt.descriptors, 0, sizeof(idt.descriptors));
  idt.descriptors[0]  = create_interrupt_gate(interrupt_handler_0);
  idt.descriptors[1]  = create_interrupt_gate(interrupt_handler_1);
  idt.descriptors[2]  = create_interrupt_gate(interrupt_handler_2);
  idt.descriptors[3]  = create_interrupt_gate(interrupt_handler_3);
  idt.descriptors[4]  = create_interrupt_gate(interrupt_handler_4);
  idt.descriptors[5]  = create_interrupt_gate(interrupt_handler_5);
  idt.descriptors[6]  = create_interrupt_gate(interrupt_handler_6);
  idt.descriptors[7]  = create_interrupt_gate(interrupt_handler_7);
  idt.descriptors[8]  = create_interrupt_gate(interrupt_handler_8);
  idt.descriptors[10] = create_interrupt_gate(interrupt_handler_10);
  idt.descriptors[11] = create_interrupt_gate(interrupt_handler_11);
  idt.descriptors[12] = create_interrupt_gate(interrupt_handler_12);
  idt.descriptors[13] = create_interrupt_gate(interrupt_handler_13);
  idt.descriptors[14] = create_interrupt_gate(interrupt_handler_14);
  idt.descriptors[16] = create_interrupt_gate(interrupt_handler_16);
  idt.descriptors[17] = create_interrupt_gate(interrupt_handler_17);
  idt.descriptors[18] = create_interrupt_gate(interrupt_handler_18);
  idt.descriptors[19] = create_interrupt_gate(interrupt_handler_19);
  idt.descriptors[20] = create_interrupt_gate(interrupt_handler_20);
  idt.descriptors[30] = create_interrupt_gate(interrupt_handler_30);
}

} // namespace hv
