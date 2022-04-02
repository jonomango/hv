.code

extern syscall_hook:proc

syscall_hook_trampoline proc
  ; execute the original bytes that we overwrote
  mov rbx, [rbp + 0C0h]
  mov rdi, [rbp + 0C8h]
  ; mov rsi, [rbp + 0D0h]

  lea rcx, [rsp + 8]

  push rax

  sub rsp, 20h
  call syscall_hook
  add rsp, 20h

  pop rax

  ret
syscall_hook_trampoline endp

end
