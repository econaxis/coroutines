section .text

global get_context_cur, set_context, get_context_par, get_context1
extern scheduler

get_context1:
    ; get return address
    push rbp
    mov rbp, rsp ; Set the RBP to the old RSP, saving it
    ; RBP now points to saved, previous RBP
    ; RBP + 8 also equals old RSP

    sub rsp, 0x8*8

    mov r8, [rbp+0x8] ; return address
    mov [rsp + 8 * 0], r8 ; set return address

    ; Now load the old RSP
    lea r8, [rbp + 0x10]
    mov [rsp + 8*1], r8

    ; Now load other non-volatile registers
    mov [rsp + 8*2], rbx


    ; Now load the RBP
    mov r8, [rbp]
    mov [rsp + 8*3], r8


    mov [rsp + 8*4], r12
    mov [rsp + 8*5], r13
    mov [rsp + 8*6], r14
    mov [rsp + 8*7], r15

    ; Load address of struct to rdi
    lea rdi, [rsp]

;    pop rbp
    call scheduler
    mov rsp, rbp



get_context_cur:
    mov r8, [rsp]
    mov [rdi + 8 * 0], r8 ; rip
    lea r8, [rsp + 8]
    mov [rdi + 8 * 1], r8 ; rsp

    mov [rdi + 8 * 2], rbx
    mov [rdi + 8 * 3], rbp
    mov [rdi + 8 * 4], r12
    mov [rdi + 8 * 5], r13
    mov [rdi + 8 * 6], r14
    mov [rdi + 8 * 7], r15

    xor eax, eax
    ret

get_context_par:
    mov r8, [rbp + 8]
    mov [rdi], r8 ; rip
;    lea r8, [rbp + 16]
    lea r8, [rsp]
    mov [rdi + 8 * 1], r8 ; rsp

    mov [rdi + 8 * 2], rbx

    mov r8, [rbp]
    mov [rdi + 8 * 3], r8
    mov [rdi + 8 * 4], r12
    mov [rdi + 8 * 5], r13
    mov [rdi + 8 * 6], r14
    mov [rdi + 8 * 7], r15
    xor eax, eax

    ret

set_context:
    mov rsp, [rdi + 8 * 1] ; temporary RSP
    mov rbx, [rdi + 8 * 2]
    mov rbp, [rdi + 8 * 3]
    mov r12, [rdi + 8 * 4]
    mov r13, [rdi + 8 * 5]
    mov r14, [rdi + 8 * 6]
    mov r15, [rdi + 8 * 7]

    mov r8, [rdi]
    push r8
    xor eax, eax
    ret


