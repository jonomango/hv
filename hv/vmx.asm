.code

?vmx_invept@hv@@YAXW4invept_type@@AEBUinvept_descriptor@@@Z proc
  invept rcx, oword ptr [rdx]
  ret
?vmx_invept@hv@@YAXW4invept_type@@AEBUinvept_descriptor@@@Z endp

?vmx_invvpid@hv@@YAXW4invvpid_type@@AEBUinvvpid_descriptor@@@Z proc
  invvpid rcx, oword ptr [rdx]
  ret
?vmx_invvpid@hv@@YAXW4invvpid_type@@AEBUinvvpid_descriptor@@@Z endp

end
