; System V AMD64: rdi, rsi, rdx, rcx, r8, r9
section .text
global outb, inb, outw, inw, cpuid, reboot, shutdown, wait_for_key

outb:
    mov rdx, rdi
    mov rax, rsi
    out dx, al
    ret

inb:
    mov rdx, rdi
    xor rax, rax
    in al, dx
    ret

outw:
    mov rdx, rdi
    mov rax, rsi
    out dx, ax
    ret

inw:
    mov rdx, rdi
    xor rax, rax
    in ax, dx
    ret

; void cpuid(uint32_t code, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d);
cpuid:
    push rbx
    push r12
    mov r12, rcx
    mov eax, edi
    xor ecx, ecx
    cpuid
    mov r9, rsi
    mov [r9], eax
    mov r9, rdx
    mov [r9], ebx
    mov r9, r12
    mov [r9], ecx
    mov r9, r8
    mov [r9], edx
    pop r12
    pop rbx
    ret

reboot:
    cli
    mov dx, 0x3F6
    mov al, 0x04
    out dx, al
    mov ecx, 100000
.delay1:
    loop .delay1
    mov al, 0x00
    out dx, al
.wait_kb:
    in al, 0x64
    test al, 2
    jnz .wait_kb
    mov al, 0xFE
    out 0x64, al
.hrb:
    hlt
    jmp .hrb

shutdown:
    cli
    mov dx, 0x604
    mov ax, 0x2000
    out dx, ax
    hlt

wait_for_key:
    in al, 0x64
    test al, 1
    jz wait_for_key
    xor rax, rax
    in al, 0x60
    ret
