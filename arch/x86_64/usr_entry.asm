section .text
global jump_user_ring3

jump_user_ring3:
    push 0x20          ; SS (data user, selector 4*8)
    push rsi           ; RSP
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push 0x18          ; CS (code user, selector 3*8)
    push rdi           ; RIP
    iretq