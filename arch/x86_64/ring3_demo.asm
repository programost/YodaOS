section .text
global ring3_demo_entry

ring3_demo_entry:
    mov rax, 1
    mov rdi, hello
    mov rsi, hello_end - hello
    int 0x80
    mov rax, 2
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    mov rax, 1
    mov rdi, pidok
    mov rsi, pidok_end - pidok
    int 0x80
.h:
    pause
    jmp .h

section .rodata
hello:
    db "[ring3] syscall write(1) OK", 10
hello_end:
pidok:
    db "[ring3] getpid syscall -> 1", 10
pidok_end:
