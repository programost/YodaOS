; Главный цикл оболочки в ring 3: только syscalls int 0x80 (обработка в kernel/syscall.c)
BITS 64
section .text
global user_ring3_main

; rax=1: write(rdi=str, rsi=len) — VGA
; rax=5: readline(rdi=buf, rsi=max) — блокирующий ввод строки
; rax=6: exec_line(rdi=buf) — разбор команды в ring 0

user_ring3_main:
.loop:
    mov rax, 1
    mov rdi, prompt
    mov rsi, prompt_len
    int 0x80

    mov rax, 5
    mov rdi, cmdbuf
    mov rsi, 256
    int 0x80

    mov rax, 6
    mov rdi, cmdbuf
    int 0x80

    jmp .loop

section .rodata
prompt:
    db "$> "
prompt_len equ 3

section .bss
align 16
cmdbuf:
    resb 256

align 16
user_r3_stack:
    resb 16384
global user_r3_stack_top
user_r3_stack_top:
