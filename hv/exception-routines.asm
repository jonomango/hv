.code

?memcpy_safe@hv@@YAXAEAUhost_exception_info@1@PEAXPEBX_K@Z proc
  mov r10, ehandler
  mov r11, rcx

  ; set exception_occurred to false
  mov byte ptr [rcx], 0

  mov rsi, r8
  mov rdi, rdx
  mov rcx, r9

  rep movsb

ehandler:
  ret
?memcpy_safe@hv@@YAXAEAUhost_exception_info@1@PEAXPEBX_K@Z endp

end
