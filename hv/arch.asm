.code

?read_cs@hv@@YA?ATsegment_selector@@XZ proc
  mov ax, cs
  ret
?read_cs@hv@@YA?ATsegment_selector@@XZ endp

?read_ss@hv@@YA?ATsegment_selector@@XZ proc
  mov ax, ss
  ret
?read_ss@hv@@YA?ATsegment_selector@@XZ endp

?read_ds@hv@@YA?ATsegment_selector@@XZ proc
  mov ax, ds
  ret
?read_ds@hv@@YA?ATsegment_selector@@XZ endp

?read_es@hv@@YA?ATsegment_selector@@XZ proc
  mov ax, es
  ret
?read_es@hv@@YA?ATsegment_selector@@XZ endp

?read_fs@hv@@YA?ATsegment_selector@@XZ proc
  mov ax, fs
  ret
?read_fs@hv@@YA?ATsegment_selector@@XZ endp

?read_gs@hv@@YA?ATsegment_selector@@XZ proc
  mov ax, gs
  ret
?read_gs@hv@@YA?ATsegment_selector@@XZ endp

?read_tr@hv@@YA?ATsegment_selector@@XZ proc
  str ax
  ret
?read_tr@hv@@YA?ATsegment_selector@@XZ endp

?read_ldtr@hv@@YA?ATsegment_selector@@XZ proc
  sldt ax
  ret
?read_ldtr@hv@@YA?ATsegment_selector@@XZ endp

?write_ds@hv@@YAXG@Z proc
  mov ds, cx
  ret
?write_ds@hv@@YAXG@Z endp

?write_es@hv@@YAXG@Z proc
  mov es, cx
  ret
?write_es@hv@@YAXG@Z endp

?write_fs@hv@@YAXG@Z proc
  mov fs, cx
  ret
?write_fs@hv@@YAXG@Z endp

?write_gs@hv@@YAXG@Z proc
  mov gs, cx
  ret
?write_gs@hv@@YAXG@Z endp

?write_tr@hv@@YAXG@Z proc
  ltr cx
  ret
?write_tr@hv@@YAXG@Z endp

?write_ldtr@hv@@YAXG@Z proc
  lldt cx
  ret
?write_ldtr@hv@@YAXG@Z endp

end

