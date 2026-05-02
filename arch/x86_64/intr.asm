section .text
global isr80_asm
extern syscall_dispatch

isr80_asm:
    mov r8, rax
    mov r9, rdi
    mov r10, rsi
    mov rdi, r8
    mov rsi, r9
    mov rdx, r10
    xor ecx, ecx
    call syscall_dispatch
    mov rdi, r9
    mov rsi, r10
    iretq
