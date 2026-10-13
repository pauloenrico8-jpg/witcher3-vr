option casemap:none
EXTERN w3vr_temporal_leaf_original:QWORD
EXTERN w3vr_temporal_leaf_after:PROC
PUBLIC w3vr_temporal_leaf_entry
.code
; Exact flag-preserving leaf only: no extra Native stack arguments.
w3vr_temporal_leaf_entry PROC FRAME
    pushfq
    .allocstack 8
    sub rsp,280h
    .allocstack 280h
    .endprolog
    mov [rsp+20h],rcx
    mov [rsp+28h],rdx
    mov rax,[rsp+288h]
    mov [rsp+30h],rax
    mov qword ptr [rsp+38h],0
    mov qword ptr [rsp+78h],0
    push qword ptr [rsp+280h]
    popfq
    call qword ptr [w3vr_temporal_leaf_original]
    mov [rsp+40h],rax
    mov [rsp+48h],rcx
    mov [rsp+50h],rdx
    mov [rsp+58h],r8
    mov [rsp+60h],r9
    mov [rsp+68h],r10
    mov [rsp+70h],r11
    vmovdqu ymmword ptr [rsp+80h],ymm0
    vmovdqu ymmword ptr [rsp+0A0h],ymm1
    vmovdqu ymmword ptr [rsp+0C0h],ymm2
    vmovdqu ymmword ptr [rsp+0E0h],ymm3
    vmovdqu ymmword ptr [rsp+100h],ymm4
    vmovdqu ymmword ptr [rsp+120h],ymm5
    vmovdqu ymmword ptr [rsp+140h],ymm6
    vmovdqu ymmword ptr [rsp+160h],ymm7
    vmovdqu ymmword ptr [rsp+180h],ymm8
    vmovdqu ymmword ptr [rsp+1A0h],ymm9
    vmovdqu ymmword ptr [rsp+1C0h],ymm10
    vmovdqu ymmword ptr [rsp+1E0h],ymm11
    vmovdqu ymmword ptr [rsp+200h],ymm12
    vmovdqu ymmword ptr [rsp+220h],ymm13
    vmovdqu ymmword ptr [rsp+240h],ymm14
    vmovdqu ymmword ptr [rsp+260h],ymm15
    lea rcx,[rsp+20h]
    call w3vr_temporal_leaf_after
    vmovdqu ymm0,ymmword ptr [rsp+80h]
    vmovdqu ymm1,ymmword ptr [rsp+0A0h]
    vmovdqu ymm2,ymmword ptr [rsp+0C0h]
    vmovdqu ymm3,ymmword ptr [rsp+0E0h]
    vmovdqu ymm4,ymmword ptr [rsp+100h]
    vmovdqu ymm5,ymmword ptr [rsp+120h]
    vmovdqu ymm6,ymmword ptr [rsp+140h]
    vmovdqu ymm7,ymmword ptr [rsp+160h]
    vmovdqu ymm8,ymmword ptr [rsp+180h]
    vmovdqu ymm9,ymmword ptr [rsp+1A0h]
    vmovdqu ymm10,ymmword ptr [rsp+1C0h]
    vmovdqu ymm11,ymmword ptr [rsp+1E0h]
    vmovdqu ymm12,ymmword ptr [rsp+200h]
    vmovdqu ymm13,ymmword ptr [rsp+220h]
    vmovdqu ymm14,ymmword ptr [rsp+240h]
    vmovdqu ymm15,ymmword ptr [rsp+260h]
    mov rax,[rsp+40h]
    mov rcx,[rsp+48h]
    mov rdx,[rsp+50h]
    mov r8,[rsp+58h]
    mov r9,[rsp+60h]
    mov r10,[rsp+68h]
    mov r11,[rsp+70h]
    add rsp,280h
    popfq
    ret
w3vr_temporal_leaf_entry ENDP
END
