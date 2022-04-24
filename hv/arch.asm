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

end

