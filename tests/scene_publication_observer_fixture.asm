option casemap:none
PUBLIC publication_observer_fixture
PUBLIC publication_observer_fixture_fp_result
PUBLIC publication_observer_fixture_clobber
PUBLIC publication_observer_fixture_invoke
PUBLIC publication_observer_fixture_after_call
PUBLIC publication_observer_fixture_invoke_fp
PUBLIC publication_observer_fixture_seed_fp
PUBLIC publication_observer_fixture_clobber_fp
.code
EXTERN publication_observer_fixture_throw_body:PROC
PUBLIC publication_observer_fixture_throw
publication_observer_fixture PROC
    REPT 16
        nop
    ENDM
    mov [rcx+10h],rax
    vmovdqu xmm0,xmmword ptr [rdx]
    vmovdqu xmmword ptr [rcx],xmm0
    mov rax,[rcx+18h]
    lea rax,[rax+1]
    mov [rcx+18h],rax
    mov rax,rcx
    ret
publication_observer_fixture ENDP
; OWN original also changes floating results, without changing arithmetic flags.
; Saving before the original would incorrectly erase these results.
publication_observer_fixture_fp_result PROC
    REPT 16
        nop
    ENDM
    mov [rcx+10h],rax
    vmovdqu xmm0,xmmword ptr [rdx]
    vmovdqu xmmword ptr [rcx],xmm0
    mov rax,[rcx+18h]
    lea rax,[rax+1]
    mov [rcx+18h],rax
    fchs
    cvtsi2ss xmm1,r8
    mov rax,rcx
    ret
publication_observer_fixture_fp_result ENDP
publication_observer_fixture_clobber PROC
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
publication_observer_fixture_clobber ENDP
publication_observer_fixture_invoke PROC FRAME
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
    mov rax,[rbx+20h]
    cmp r8,r9
    call rsi
publication_observer_fixture_after_call::
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
publication_observer_fixture_invoke ENDP

; OWN floating environment harness. The called fixture/bridge remains two-pointer.
; Save the C++ caller's environment, seed another one, capture immediately, restore.
publication_observer_fixture_invoke_fp PROC FRAME
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    sub rsp,228h
    .allocstack 228h
    .endprolog
    mov rbx,rcx
    mov rsi,rdx
    mov rdi,r8
    mov r12,r9
    fxsave64 [rsp+20h]
    fxrstor64 [rdi]
    mov rcx,rbx
    mov rdx,rsi
    call publication_observer_fixture_invoke
    fxsave64 [r12]
    fxrstor64 [rsp+20h]
    add rsp,228h
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret
publication_observer_fixture_invoke_fp ENDP
; Real x87 values/status/tag state, made entirely by OWN instructions.
publication_observer_fixture_seed_fp PROC
    fninit
    fld1
    fldz
    fldpi
    fldln2
    fcom st(1)
    ret
publication_observer_fixture_seed_fp ENDP
; Deliberately damages control, status, tags and x87 data in the observer.
publication_observer_fixture_clobber_fp PROC
    fninit
    fld1
    fldpi
    faddp st(1),st(0)
    ret
publication_observer_fixture_clobber_fp ENDP
publication_observer_fixture_throw PROC
    REPT 16
        nop
    ENDM
    jmp publication_observer_fixture_throw_body
publication_observer_fixture_throw ENDP
END
