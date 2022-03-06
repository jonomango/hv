.code

?vmx_invept@hv@@YAXW4invept_type@@AEBUinvept_descriptor@@@Z proc
  invept rcx, oword ptr [rdx]
  ret
?vmx_invept@hv@@YAXW4invept_type@@AEBUinvept_descriptor@@@Z endp

?vmx_invvpid@hv@@YAXW4invvpid_type@@AEBUinvvpid_descriptor@@@Z proc
  invvpid rcx, oword ptr [rdx]
  ret
?vmx_invvpid@hv@@YAXW4invvpid_type@@AEBUinvvpid_descriptor@@@Z endp

?vmx_vmcall@hv@@YA_KAEAUhypercall_input@1@@Z proc
  ; move input into registers
  mov rax, [rcx]       ; code
  mov rdx, [rcx + 10h] ; args[1]
  mov r8,  [rcx + 18h] ; args[2]
  mov r9,  [rcx + 20h] ; args[3]
  mov r10, [rcx + 28h] ; args[4]
  mov r11, [rcx + 30h] ; args[5]
  mov rcx, [rcx + 08h] ; args[0]

  vmcall

  ret
?vmx_vmcall@hv@@YA_KAEAUhypercall_input@1@@Z endp

end
