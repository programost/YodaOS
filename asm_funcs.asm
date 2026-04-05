; asm_funcs.asm
section .text
global outb, inb, outw, inw, cpuid, reboot, shutdown, wait_for_key

outb:
    mov dx, [esp+4]
    mov al, [esp+8]
    out dx, al
    ret

inb:
    mov dx, [esp+4]
    in al, dx
    ret

outw:
    mov dx, [esp+4]
    mov ax, [esp+8]
    out dx, ax
    ret

inw:
    mov dx, [esp+4]
    in ax, dx
    ret

cpuid:
    push ebx
    mov eax, [esp+8]
    cpuid
    mov ebx, [esp+12]
    mov [ebx], eax
    mov ebx, [esp+16]
    mov [ebx], ebx
    mov ebx, [esp+20]
    mov [ebx], ecx
    mov ebx, [esp+24]
    mov [ebx], edx
    pop ebx
    ret

reboot:
    cli
    mov al, 0xFE
    out 0x64, al
    jmp 0xffff:0x0000

shutdown:
    cli
    mov dx, 0x604
    mov ax, 0x2000
    out dx, ax
    hlt

wait_for_key:
    xor eax, eax
    in al, 0x64
    test al, 1
    jz wait_for_key
    in al, 0x60
    ret