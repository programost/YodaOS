; boot.asm - Multiboot compliant bootloader for YodaOS (flat binary)
; Compile: nasm -f elf32 boot.asm -o boot.o

section .multiboot
align 4
    dd 0x1BADB002          ; magic
    dd 0x03                ; flags (align, meminfo)
    dd -(0x1BADB002+0x03)  ; checksum

section .text
global start
extern kmain

start:
    mov esp, stack_top     ; setup stack
    push eax               ; pass multiboot magic
    push ebx               ; pass multiboot info structure
    call kmain
    cli
    hlt

section .bss
align 16
stack_bottom:
    resb 16384             ; 16 KB stack
stack_top: