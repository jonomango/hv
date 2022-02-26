.code

; defined in trap-frame.h
trap_frame struct
  ; general-purpose registers
  $rax qword ?
  $rcx qword ?
  $rdx qword ?
  $rbx qword ?
  $rbp qword ?
  $rsi qword ?
  $rdi qword ?
  $r8  qword ?
  $r9  qword ?
  $r10 qword ?
  $r11 qword ?
  $r12 qword ?
  $r13 qword ?
  $r14 qword ?
  $r15 qword ?

  ; interrupt vector
  $vector qword ?

  ; _MACHINE_FRAME
  $error  qword ?
  $rip    qword ?
  $cs     qword ?
  $rflags qword ?
  $rsp    qword ?
  $ss     qword ?
trap_frame ends

extern ?handle_host_interrupt@hv@@YAXQEAUtrap_frame@1@@Z : proc

; the generic interrupt handler that every stub will eventually jump to
generic_interrupt_handler proc
  ; allocate space for the trap_frame structure (minus the size of the
  ; _MACHINE_FRAME, error code, and interrupt vector)
  sub rsp, 78h

  ; general-purpose registers
  mov trap_frame.$rax[rsp], rax
  mov trap_frame.$rcx[rsp], rcx
  mov trap_frame.$rdx[rsp], rdx
  mov trap_frame.$rbx[rsp], rbx
  mov trap_frame.$rbp[rsp], rbp
  mov trap_frame.$rsi[rsp], rsi
  mov trap_frame.$rdi[rsp], rdi
  mov trap_frame.$r8[rsp],  r8
  mov trap_frame.$r9[rsp],  r9
  mov trap_frame.$r10[rsp], r10
  mov trap_frame.$r11[rsp], r11
  mov trap_frame.$r12[rsp], r12
  mov trap_frame.$r13[rsp], r13
  mov trap_frame.$r14[rsp], r14
  mov trap_frame.$r15[rsp], r15

  ; first argument is the trap frame
  mov rcx, rsp

  ; call handle_host_interrupt
  sub rsp, 20h
  call ?handle_host_interrupt@hv@@YAXQEAUtrap_frame@1@@Z
  add rsp, 20h

  ; general-purpose registers
  mov rax, trap_frame.$rax[rsp]
  mov rcx, trap_frame.$rcx[rsp]
  mov rdx, trap_frame.$rdx[rsp]
  mov rbx, trap_frame.$rbx[rsp]
  mov rbp, trap_frame.$rbp[rsp]
  mov rsi, trap_frame.$rsi[rsp]
  mov rdi, trap_frame.$rdi[rsp]
  mov r8,  trap_frame.$r8[rsp]
  mov r9,  trap_frame.$r9[rsp]
  mov r10, trap_frame.$r10[rsp]
  mov r11, trap_frame.$r11[rsp]
  mov r12, trap_frame.$r12[rsp]
  mov r13, trap_frame.$r13[rsp]
  mov r14, trap_frame.$r14[rsp]
  mov r15, trap_frame.$r15[rsp]

  ; free the trap_frame
  add rsp, 78h

  ; pop the interrupt vector
  add rsp, 8

  ; pop the error code
  add rsp, 8

  iretq
generic_interrupt_handler endp

; pushes error code to stack
DEFINE_ISR macro interrupt_vector:req, proc_name:req
proc_name proc
  ; interrupt vector is stored right before the machine frame
  push interrupt_vector

  jmp generic_interrupt_handler
proc_name endp
endm

; doesn't push error code to stack
DEFINE_ISR_NO_ERROR macro interrupt_vector:req, proc_name:req
proc_name proc
  ; push a dummy error code onto the stack
  push 0

  ; interrupt vector is stored right before the machine frame
  push interrupt_vector

  jmp generic_interrupt_handler
proc_name endp
endm

DEFINE_ISR_NO_ERROR 0,  ?interrupt_handler_0@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 1,  ?interrupt_handler_1@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 2,  ?interrupt_handler_2@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 3,  ?interrupt_handler_3@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 4,  ?interrupt_handler_4@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 5,  ?interrupt_handler_5@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 6,  ?interrupt_handler_6@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 7,  ?interrupt_handler_7@hv@@YAXXZ
DEFINE_ISR          8,  ?interrupt_handler_8@hv@@YAXXZ
DEFINE_ISR          10, ?interrupt_handler_10@hv@@YAXXZ
DEFINE_ISR          11, ?interrupt_handler_11@hv@@YAXXZ
DEFINE_ISR          12, ?interrupt_handler_12@hv@@YAXXZ
DEFINE_ISR          13, ?interrupt_handler_13@hv@@YAXXZ
DEFINE_ISR          14, ?interrupt_handler_14@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 16, ?interrupt_handler_16@hv@@YAXXZ
DEFINE_ISR          17, ?interrupt_handler_17@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 18, ?interrupt_handler_18@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 19, ?interrupt_handler_19@hv@@YAXXZ
DEFINE_ISR_NO_ERROR 20, ?interrupt_handler_20@hv@@YAXXZ
DEFINE_ISR          30, ?interrupt_handler_30@hv@@YAXXZ

end