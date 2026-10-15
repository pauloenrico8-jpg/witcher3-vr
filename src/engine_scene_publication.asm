option casemap:none
PUBLIC w3vr_invoke_scene_publication
.code
; OWN Win64 ABI (Original, command)->raw instruction result. This does not
; declare the Native procedure's return type or read recyclable command memory.
w3vr_invoke_scene_publication PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp,20h
    .allocstack 20h
    .endprolog
    mov rbx,rcx
    mov rcx,rdx
    xor eax,eax
    call rbx
    add rsp,20h
    pop rbx
    ret
w3vr_invoke_scene_publication ENDP
END
