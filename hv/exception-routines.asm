.code

?memcpy_safe@hv@@YAXAEAUhost_exception_info@1@PEAXPEBX_K@Z proc
  mov r10, ehandler
  mov r11, rcx
  mov byte ptr [rcx], 0

  ; store RSI and RDI
  push rsi
  push rdi

  mov rsi, r8
  mov rdi, rdx
  mov rcx, r9

  rep movsb

ehandler:
  ; restore RDI and RSI
  pop rdi
  pop rsi

  ret
?memcpy_safe@hv@@YAXAEAUhost_exception_info@1@PEAXPEBX_K@Z endp

?xsetbv_safe@hv@@YAXAEAUhost_exception_info@1@I_K@Z proc
  mov r10, ehandler
  mov r11, rcx
  mov byte ptr [rcx], 0

  ; idx
  mov ecx, edx

  ; value (low part)
  mov eax, r8d

  ; value (high part)
  mov rdx, r8
  shr rdx, 32

  xsetbv

ehandler:
  ret
?xsetbv_safe@hv@@YAXAEAUhost_exception_info@1@I_K@Z endp

?wrmsr_safe@hv@@YAXAEAUhost_exception_info@1@I_K@Z proc
  mov r10, ehandler
  mov r11, rcx
  mov byte ptr [rcx], 0

  ; msr
  mov ecx, edx

  ; value
  mov eax, r8d
  mov rdx, r8
  shr rdx, 32

  wrmsr

ehandler:
  ret
?wrmsr_safe@hv@@YAXAEAUhost_exception_info@1@I_K@Z endp

?rdmsr_safe@hv@@YA_KAEAUhost_exception_info@1@I@Z proc
  mov r10, ehandler
  mov r11, rcx
  mov byte ptr [rcx], 0

  ; msr
  mov ecx, edx

  rdmsr

  ; return value
  shl rdx, 32
  and rax, rdx

ehandler:
  ret
?rdmsr_safe@hv@@YA_KAEAUhost_exception_info@1@I@Z endp

end

