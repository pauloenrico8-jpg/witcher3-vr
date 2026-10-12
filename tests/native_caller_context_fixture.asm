option dotname
option casemap:none
extern lab_callback:proc
public lab_root, lab_root_fragment, lab_root_after_call
public lab_worker, lab_worker_after_call
.code
; Entirely OWN code/metadata. No game code, injection or dynamic registration.
lab_root proc
    push rbp
root_saved_rbp::
    push rdi
root_saved_rdi::
    push rbx
root_saved_rbx::
    sub rsp, 0a0h
root_allocated::
    lea rbp, [rsp + 090h]
root_frame_set::
lab_root_fragment::
    mov rdi, rcx
    mov rbx, rdx
    mov qword ptr [rbp - 050h], rdx
    mov qword ptr [rbp - 048h], r8
    lea rdx, [rbp - 050h]
    call lab_worker
lab_root_after_call::
    nop
    add rsp, 0a0h
    pop rbx
    pop rdi
    pop rbp
    ret
lab_root_end::
lab_root endp
lab_worker proc
    push rbp
worker_saved_rbp::
    push rdi
worker_saved_rdi::
    sub rsp, 028h
worker_allocated::
worker_part1::
    mov rdi, 01122334455667788h
worker_part2::
    mov qword ptr [rsp + 020h], rbx
worker_late_saved_rbx::
    mov rbx, 07766554433221100h
worker_part3::
    xor ebp, ebp
worker_part4::
    call lab_callback
lab_worker_after_call::
    nop
    mov rbx, qword ptr [rsp + 020h]
    add rsp, 028h
    pop rdi
    pop rbp
    ret
lab_worker_end::
lab_worker endp
.xdata segment readonly
align 4
root_unwind:
    db 01h, root_frame_set-lab_root, 6, 095h
    db root_frame_set-lab_root, 03h
    db root_allocated-lab_root, 01h
    dw 014h
    db root_saved_rbx-lab_root, 030h
    db root_saved_rdi-lab_root, 070h
    db root_saved_rbp-lab_root, 050h
align 4
root_fragment_unwind:
    db 021h, 0, 0, 095h
    dd imagerel lab_root, imagerel lab_root_fragment, imagerel root_unwind
align 4
worker_unwind:
    db 01h, worker_allocated-lab_worker, 3, 0
    db worker_allocated-lab_worker, 042h
    db worker_saved_rdi-lab_worker, 070h
    db worker_saved_rbp-lab_worker, 050h
    dw 0
align 4
worker_part1_unwind:
    db 021h, 0, 0, 0
    dd imagerel lab_worker, imagerel worker_part1, imagerel worker_unwind
align 4
worker_part2_unwind:
    db 021h, worker_late_saved_rbx-worker_part2, 2, 0
    db worker_late_saved_rbx-worker_part2, 034h
    dw 4
    dd imagerel worker_part1, imagerel worker_part2, imagerel worker_part1_unwind
align 4
worker_part3_unwind:
    db 021h, 0, 0, 0
    dd imagerel worker_part2, imagerel worker_part3, imagerel worker_part2_unwind
align 4
worker_part4_unwind:
    db 021h, 0, 0, 0
    dd imagerel worker_part3, imagerel worker_part4, imagerel worker_part3_unwind
.xdata ends
.pdata segment readonly
align 4
    dd imagerel lab_root, imagerel lab_root_fragment, imagerel root_unwind
    dd imagerel lab_root_fragment, imagerel lab_root_end, imagerel root_fragment_unwind
    dd imagerel lab_worker, imagerel worker_part1, imagerel worker_unwind
    dd imagerel worker_part1, imagerel worker_part2, imagerel worker_part1_unwind
    dd imagerel worker_part2, imagerel worker_part3, imagerel worker_part2_unwind
    dd imagerel worker_part3, imagerel worker_part4, imagerel worker_part3_unwind
    dd imagerel worker_part4, imagerel lab_worker_end, imagerel worker_part4_unwind
.pdata ends
end
