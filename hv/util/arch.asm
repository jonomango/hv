.code

; hv::__vmx_invept
?__vmx_invept@hv@@YAXW4invept_type@@AEBUinvept_descriptor@@@Z proc
  invept rcx, oword ptr [rdx]
  ret
?__vmx_invept@hv@@YAXW4invept_type@@AEBUinvept_descriptor@@@Z endp

end
