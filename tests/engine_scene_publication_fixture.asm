option casemap:none
EXTERN w3vr_publication_throw_fixture:PROC
PUBLIC w3vr_publication_fixture
.code
; Entirely OWN fixture. XADD (not the Game instruction sequence) produces
; a prior-value register while an immediate reuse overwrites the memory.
w3vr_publication_fixture PROC
    mov [rcx+10h],rax
    mov [rcx+20h],rcx
    inc dword ptr [rcx+4]
    cmp dword ptr [rcx],1
    je no_write
    cmp dword ptr [rcx],2
    je sentinel
    cmp dword ptr [rcx],4
    je throw_path
    mov edx,1
    lock xadd dword ptr [rcx+8],edx
    mov eax,edx
    mov qword ptr [rcx+8],0
    ret
no_write:
    ret
sentinel:
    mov rax,[rcx+18h]
    ret
throw_path:
    jmp w3vr_publication_throw_fixture
w3vr_publication_fixture ENDP
END
