section .text
global ring3_demo_entry

ring3_demo_entry:
    mov rax, 1          ; sys_write
    mov rdi, 1
    lea rsi, [rel hello]
    mov rdx, hello_end - hello
    syscall

    mov rax, 2          ; sys_getpid
    syscall

    mov rax, 1
    mov rdi, 1
    lea rsi, [rel pidok]
    mov rdx, pidok_end - pidok
    syscall

    mov rax, 60         ; sys_exit
    xor rdi, rdi
    syscall

.h:
    jmp .h

section .rodata
hello: db "[ring3] syscall write(1) OK", 10
hello_end:
pidok: db "[ring3] getpid syscall -> 1", 10
pidok_end: