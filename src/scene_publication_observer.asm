option casemap:none
EXTERN w3vr_scene_publication_original:QWORD
EXTERN w3vr_scene_publication_before:PROC
EXTERN w3vr_scene_publication_after:PROC
PUBLIC w3vr_scene_publication_entry
.code
; Preserve incoming AND outgoing numeric/register state around OWN observers.
; The Native one-argument procedure is called exactly once, without a seed.
w3vr_scene_publication_entry PROC FRAME
    pushfq
    .allocstack 8
    sub rsp,4C0h
    .allocstack 4C0h
    .endprolog
    mov [rsp+28h],rax
    mov [rsp+30h],rcx
    mov [rsp+38h],rdx
    mov [rsp+40h],r8
    mov [rsp+48h],r9
    mov [rsp+50h],r10
    mov [rsp+58h],r11
    mov rax,[rsp+4C8h]
    mov [rsp+20h],rax
    mov qword ptr [rsp+98h],0
    mov qword ptr [rsp+0A0h],0
    mov qword ptr [rsp+0A8h],0
    mov qword ptr [rsp+0B0h],0
    vmovdqu ymmword ptr [rsp+0C0h],ymm0
    vmovdqu ymmword ptr [rsp+0E0h],ymm1
    vmovdqu ymmword ptr [rsp+100h],ymm2
    vmovdqu ymmword ptr [rsp+120h],ymm3
    vmovdqu ymmword ptr [rsp+140h],ymm4
    vmovdqu ymmword ptr [rsp+160h],ymm5
    vmovdqu ymmword ptr [rsp+180h],ymm6
    vmovdqu ymmword ptr [rsp+1A0h],ymm7
    vmovdqu ymmword ptr [rsp+1C0h],ymm8
    vmovdqu ymmword ptr [rsp+1E0h],ymm9
    vmovdqu ymmword ptr [rsp+200h],ymm10
    vmovdqu ymmword ptr [rsp+220h],ymm11
    vmovdqu ymmword ptr [rsp+240h],ymm12
    vmovdqu ymmword ptr [rsp+260h],ymm13
    vmovdqu ymmword ptr [rsp+280h],ymm14
    vmovdqu ymmword ptr [rsp+2A0h],ymm15
    fxsave64 [rsp+2C0h]
    lea rcx,[rsp+20h]
    call w3vr_scene_publication_before
    fxrstor64 [rsp+2C0h]
    vmovdqu ymm0,ymmword ptr [rsp+0C0h]
    vmovdqu ymm1,ymmword ptr [rsp+0E0h]
    vmovdqu ymm2,ymmword ptr [rsp+100h]
    vmovdqu ymm3,ymmword ptr [rsp+120h]
    vmovdqu ymm4,ymmword ptr [rsp+140h]
    vmovdqu ymm5,ymmword ptr [rsp+160h]
    vmovdqu ymm6,ymmword ptr [rsp+180h]
    vmovdqu ymm7,ymmword ptr [rsp+1A0h]
    vmovdqu ymm8,ymmword ptr [rsp+1C0h]
    vmovdqu ymm9,ymmword ptr [rsp+1E0h]
    vmovdqu ymm10,ymmword ptr [rsp+200h]
    vmovdqu ymm11,ymmword ptr [rsp+220h]
    vmovdqu ymm12,ymmword ptr [rsp+240h]
    vmovdqu ymm13,ymmword ptr [rsp+260h]
    vmovdqu ymm14,ymmword ptr [rsp+280h]
    vmovdqu ymm15,ymmword ptr [rsp+2A0h]
    mov rax,[rsp+28h]
    mov rcx,[rsp+30h]
    mov rdx,[rsp+38h]
    mov r8,[rsp+40h]
    mov r9,[rsp+48h]
    mov r10,[rsp+50h]
    mov r11,[rsp+58h]
    push qword ptr [rsp+4C0h]
    popfq
    call qword ptr [w3vr_scene_publication_original]
    mov [rsp+60h],rax
    mov [rsp+68h],rcx
    mov [rsp+70h],rdx
    mov [rsp+78h],r8
    mov [rsp+80h],r9
    mov [rsp+88h],r10
    mov [rsp+90h],r11
    pushfq
    pop qword ptr [rsp+4C0h]
    vmovdqu ymmword ptr [rsp+0C0h],ymm0
    vmovdqu ymmword ptr [rsp+0E0h],ymm1
    vmovdqu ymmword ptr [rsp+100h],ymm2
    vmovdqu ymmword ptr [rsp+120h],ymm3
    vmovdqu ymmword ptr [rsp+140h],ymm4
    vmovdqu ymmword ptr [rsp+160h],ymm5
    vmovdqu ymmword ptr [rsp+180h],ymm6
    vmovdqu ymmword ptr [rsp+1A0h],ymm7
    vmovdqu ymmword ptr [rsp+1C0h],ymm8
    vmovdqu ymmword ptr [rsp+1E0h],ymm9
    vmovdqu ymmword ptr [rsp+200h],ymm10
    vmovdqu ymmword ptr [rsp+220h],ymm11
    vmovdqu ymmword ptr [rsp+240h],ymm12
    vmovdqu ymmword ptr [rsp+260h],ymm13
    vmovdqu ymmword ptr [rsp+280h],ymm14
    vmovdqu ymmword ptr [rsp+2A0h],ymm15
    fxsave64 [rsp+2C0h]
    lea rcx,[rsp+20h]
    call w3vr_scene_publication_after
    fxrstor64 [rsp+2C0h]
    vmovdqu ymm0,ymmword ptr [rsp+0C0h]
    vmovdqu ymm1,ymmword ptr [rsp+0E0h]
    vmovdqu ymm2,ymmword ptr [rsp+100h]
    vmovdqu ymm3,ymmword ptr [rsp+120h]
    vmovdqu ymm4,ymmword ptr [rsp+140h]
    vmovdqu ymm5,ymmword ptr [rsp+160h]
    vmovdqu ymm6,ymmword ptr [rsp+180h]
    vmovdqu ymm7,ymmword ptr [rsp+1A0h]
    vmovdqu ymm8,ymmword ptr [rsp+1C0h]
    vmovdqu ymm9,ymmword ptr [rsp+1E0h]
    vmovdqu ymm10,ymmword ptr [rsp+200h]
    vmovdqu ymm11,ymmword ptr [rsp+220h]
    vmovdqu ymm12,ymmword ptr [rsp+240h]
    vmovdqu ymm13,ymmword ptr [rsp+260h]
    vmovdqu ymm14,ymmword ptr [rsp+280h]
    vmovdqu ymm15,ymmword ptr [rsp+2A0h]
    mov rax,[rsp+60h]
    mov rcx,[rsp+68h]
    mov rdx,[rsp+70h]
    mov r8,[rsp+78h]
    mov r9,[rsp+80h]
    mov r10,[rsp+88h]
    mov r11,[rsp+90h]
    lea rsp,[rsp+4C0h]
    popfq
    ret
w3vr_scene_publication_entry ENDP
END
