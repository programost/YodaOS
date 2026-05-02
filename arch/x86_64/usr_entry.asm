section .text
global jump_user_ring3

jump_user_ring3:
    push 0x23
    push rsi
    pushfq
    or qword [rsp], 0x200
    push 0x1B
    push rdi
    iretq
