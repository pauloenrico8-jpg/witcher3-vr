option casemap:none
PUBLIC temporal_leaf_fixture
PUBLIC temporal_leaf_fixture_clobber
PUBLIC temporal_leaf_fixture_invoke
PUBLIC temporal_leaf_fixture_after_call
.code
temporal_leaf_fixture PROC
    REPT 16
        nop
    ENDM
    vmovdqu xmm0,xmmword ptr [rdx]
    vmovdqu xmmword ptr [rcx],xmm0
    mov rax,[rcx+18h]
    lea rax,[rax+1]
    mov [rcx+18h],rax
    mov rax,rcx
    ret
temporal_leaf_fixture ENDP
temporal_leaf_fixture_clobber PROC
    xor rax,rax
    xor rcx,rcx
    xor rdx,rdx
    xor r8,r8
    xor r9,r9
    xor r10,r10
    xor r11,r11
    vxorps ymm0,ymm0,ymm0
    vxorps ymm1,ymm1,ymm1
    vxorps ymm2,ymm2,ymm2
    vxorps ymm3,ymm3,ymm3
    vxorps ymm4,ymm4,ymm4
    vxorps ymm5,ymm5,ymm5
    vzeroupper
    ret
temporal_leaf_fixture_clobber ENDP
temporal_leaf_fixture_invoke PROC FRAME
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    sub rsp,0C8h
    .allocstack 0C8h
    vmovdqu xmmword ptr [rsp+20h],xmm6
    .savexmm128 xmm6,20h
    vmovdqu xmmword ptr [rsp+30h],xmm7
    .savexmm128 xmm7,30h
    vmovdqu xmmword ptr [rsp+40h],xmm8
    .savexmm128 xmm8,40h
    vmovdqu xmmword ptr [rsp+50h],xmm9
    .savexmm128 xmm9,50h
    vmovdqu xmmword ptr [rsp+60h],xmm10
    .savexmm128 xmm10,60h
    vmovdqu xmmword ptr [rsp+70h],xmm11
    .savexmm128 xmm11,70h
    vmovdqu xmmword ptr [rsp+80h],xmm12
    .savexmm128 xmm12,80h
    vmovdqu xmmword ptr [rsp+90h],xmm13
    .savexmm128 xmm13,90h
    vmovdqu xmmword ptr [rsp+0A0h],xmm14
    .savexmm128 xmm14,0A0h
    vmovdqu xmmword ptr [rsp+0B0h],xmm15
    .savexmm128 xmm15,0B0h
    .endprolog
    mov rbx,rcx
    mov rsi,rdx
    vmovdqu ymm0,ymmword ptr [rbx+60h]
    vmovdqu ymm1,ymmword ptr [rbx+80h]
    vmovdqu ymm2,ymmword ptr [rbx+0A0h]
    vmovdqu ymm3,ymmword ptr [rbx+0C0h]
    vmovdqu ymm4,ymmword ptr [rbx+0E0h]
    vmovdqu ymm5,ymmword ptr [rbx+100h]
    vmovdqu ymm6,ymmword ptr [rbx+120h]
    vmovdqu ymm7,ymmword ptr [rbx+140h]
    vmovdqu ymm8,ymmword ptr [rbx+160h]
    vmovdqu ymm9,ymmword ptr [rbx+180h]
    vmovdqu ymm10,ymmword ptr [rbx+1A0h]
    vmovdqu ymm11,ymmword ptr [rbx+1C0h]
    vmovdqu ymm12,ymmword ptr [rbx+1E0h]
    vmovdqu ymm13,ymmword ptr [rbx+200h]
    vmovdqu ymm14,ymmword ptr [rbx+220h]
    vmovdqu ymm15,ymmword ptr [rbx+240h]
    mov rcx,[rbx+28h]
    mov rdx,[rbx+30h]
    mov r8,[rbx+38h]
    mov r9,[rbx+40h]
    mov r10,[rbx+48h]
    mov r11,[rbx+50h]
    cmp r8,r9
    call rsi
temporal_leaf_fixture_after_call::
    mov [rbx+20h],rax
    mov [rbx+28h],rcx
    mov [rbx+30h],rdx
    mov [rbx+38h],r8
    mov [rbx+40h],r9
    mov [rbx+48h],r10
    mov [rbx+50h],r11
    pushfq
    pop qword ptr [rbx+18h]
    vmovdqu ymmword ptr [rbx+60h],ymm0
    vmovdqu ymmword ptr [rbx+80h],ymm1
    vmovdqu ymmword ptr [rbx+0A0h],ymm2
    vmovdqu ymmword ptr [rbx+0C0h],ymm3
    vmovdqu ymmword ptr [rbx+0E0h],ymm4
    vmovdqu ymmword ptr [rbx+100h],ymm5
    vmovdqu ymmword ptr [rbx+120h],ymm6
    vmovdqu ymmword ptr [rbx+140h],ymm7
    vmovdqu ymmword ptr [rbx+160h],ymm8
    vmovdqu ymmword ptr [rbx+180h],ymm9
    vmovdqu ymmword ptr [rbx+1A0h],ymm10
    vmovdqu ymmword ptr [rbx+1C0h],ymm11
    vmovdqu ymmword ptr [rbx+1E0h],ymm12
    vmovdqu ymmword ptr [rbx+200h],ymm13
    vmovdqu ymmword ptr [rbx+220h],ymm14
    vmovdqu ymmword ptr [rbx+240h],ymm15
    vmovdqu xmm6,xmmword ptr [rsp+20h]
    vmovdqu xmm7,xmmword ptr [rsp+30h]
    vmovdqu xmm8,xmmword ptr [rsp+40h]
    vmovdqu xmm9,xmmword ptr [rsp+50h]
    vmovdqu xmm10,xmmword ptr [rsp+60h]
    vmovdqu xmm11,xmmword ptr [rsp+70h]
    vmovdqu xmm12,xmmword ptr [rsp+80h]
    vmovdqu xmm13,xmmword ptr [rsp+90h]
    vmovdqu xmm14,xmmword ptr [rsp+0A0h]
    vmovdqu xmm15,xmmword ptr [rsp+0B0h]
    add rsp,0C8h
    pop rsi
    pop rbx
    ret
temporal_leaf_fixture_invoke ENDP
END
