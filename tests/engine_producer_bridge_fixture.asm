option casemap:none
PUBLIC producer_fixture_target
PUBLIC producer_fixture_clobber
.code
; Independent OWN target records the actual incoming registers into its OWN
; context, then returns the prescribed integer/vector bits. MinHook patches
; only this function in the test executable, never Game/SDK/OS functions.
producer_fixture_target PROC FRAME
    sub rsp,28h
    .allocstack 28h
    .endprolog
    REPT 24
      nop
    ENDM
    mov [rcx],rcx
    mov [rcx+8],rdx
    mov [rcx+10h],r8
    mov [rcx+18h],r9
    vmovdqu ymmword ptr [rcx+20h],ymm0
    vmovdqu ymmword ptr [rcx+40h],ymm1
    vmovdqu ymmword ptr [rcx+60h],ymm2
    vmovdqu ymmword ptr [rcx+80h],ymm3
    inc qword ptr [rcx+260h]
    mov rax,[rcx+1D0h]
    mov rdx,[rcx+1D8h]
    vmovdqu ymm0,ymmword ptr [rcx+1E0h]
    vmovdqu ymm1,ymmword ptr [rcx+200h]
    vmovdqu ymm2,ymmword ptr [rcx+220h]
    vmovdqu ymm3,ymmword ptr [rcx+240h]
    add rsp,28h
    ret
producer_fixture_target ENDP
producer_fixture_clobber PROC
    mov rax,0BADBADBADh
    mov rdx,0DEADDEADDEADh
    mov rcx,0111222333h
    mov r8,0444555666h
    mov r9,0777888999h
    vxorps ymm0,ymm0,ymm0
    vcmpeqps ymm0,ymm0,ymm0
    vxorps ymm1,ymm1,ymm1
    vmovdqu ymm2,ymm0
    vmovdqu ymm3,ymm1
    ret
producer_fixture_clobber ENDP
END
