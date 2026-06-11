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

global syscall_setup
syscall_setup:
    ; STAR MSR (0xC0000081): 32-47 = CS, 48-63 = SS для sysret
    ; Для 64-bit: нижние 32 бита = CS пользователя (0x1B), верхние 32 бита = CS ядра (0x08)
    mov ecx, 0xC0000081
    mov edx, 0x001B0008   ; CS user = 0x1B (code32?), но для 64-bit нужно другое
    mov eax, 0x00000000
    wrmsr

    ; LSTAR (0xC0000082): адрес обработчика syscall
    mov ecx, 0xC0000082
    mov rax, syscall_entry
    mov rdx, rax
    shr rdx, 32
    wrmsr

    ; SFMASK (0xC0000084): маска RFLAGS для syscall (сбросить IF и другие)
    mov ecx, 0xC0000084
    mov eax, 0x200         ; сбросить IF
    xor edx, edx
    wrmsr
    ret

extern syscall_entry
