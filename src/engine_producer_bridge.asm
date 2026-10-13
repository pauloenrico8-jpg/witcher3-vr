option casemap:none
EXTERN w3vr_producer_scope_body:PROC
PUBLIC w3vr_producer_entry
PUBLIC w3vr_invoke_producer_original
PUBLIC w3vr_invoke_producer_after_call
.code
; No C++ return conversion occurs at the Native entry/exit. The OWN sidecar
; runs around the original, whose results are saved before cleanup can clobber.
w3vr_producer_entry PROC FRAME
    sub rsp,158h
    .allocstack 158h
    .endprolog
    mov [rsp+20h],rcx
    mov [rsp+28h],rdx
    mov [rsp+30h],r8
    mov [rsp+38h],r9
    vmovdqu ymmword ptr [rsp+40h],ymm0
    vmovdqu ymmword ptr [rsp+60h],ymm1
    vmovdqu ymmword ptr [rsp+80h],ymm2
    vmovdqu ymmword ptr [rsp+0A0h],ymm3
    lea rcx,[rsp+20h]
    call w3vr_producer_scope_body
    mov rax,[rsp+0C0h]
    mov rdx,[rsp+0C8h]
    vmovdqu ymm0,ymmword ptr [rsp+0D0h]
    vmovdqu ymm1,ymmword ptr [rsp+0F0h]
    vmovdqu ymm2,ymmword ptr [rsp+110h]
    vmovdqu ymm3,ymmword ptr [rsp+130h]
    add rsp,158h
    ret
w3vr_producer_entry ENDP

w3vr_invoke_producer_original PROC FRAME
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    sub rsp,28h
    .allocstack 28h
    .endprolog
    mov rbx,rcx
    mov rsi,rdx
    mov rcx,[rsi]
    mov rdx,[rsi+8]
    mov r8,[rsi+10h]
    mov r9,[rsi+18h]
    vmovdqu ymm0,ymmword ptr [rsi+20h]
    vmovdqu ymm1,ymmword ptr [rsi+40h]
    vmovdqu ymm2,ymmword ptr [rsi+60h]
    vmovdqu ymm3,ymmword ptr [rsi+80h]
    call rbx
w3vr_invoke_producer_after_call::
    mov [rsi+0A0h],rax
    mov [rsi+0A8h],rdx
    vmovdqu ymmword ptr [rsi+0B0h],ymm0
    vmovdqu ymmword ptr [rsi+0D0h],ymm1
    vmovdqu ymmword ptr [rsi+0F0h],ymm2
    vmovdqu ymmword ptr [rsi+110h],ymm3
    add rsp,28h
    pop rsi
    pop rbx
    ret
w3vr_invoke_producer_original ENDP
END
