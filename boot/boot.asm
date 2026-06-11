; Multiboot2 + переход в long mode (x86_64), затем вызов kmain(rdi=magic, rsi=mbi phys)
BITS 32

section .multiboot align=8
mb_header:
    dd 0xE85250D6
    dd 0
    dd mb_header_end - mb_header
    dd -(0xE85250D6 + 0 + (mb_header_end - mb_header))
    dw 0, 0
    dd 8
mb_header_end:

section .bss
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096
align 16
stack_bottom:
    resb 32768
stack_top:

section .data
align 4
saved_magic: dd 0
saved_mbi:   dd 0

section .rodata
align 8
gdt_start:
    dq 0
    dq 0x00AF9A000000FFFF   ; code kernel (DPL=0)
    dq 0x00CF92000000FFFF   ; data kernel (DPL=0)
    dq 0x00AFFA000000FFFF   ; code user (DPL=3)  -> индекс 3 (0x18)
    dq 0x00CFF2000000FFFF   ; data user (DPL=3)  -> индекс 4 (0x20)
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEL equ 0x08

section .text
global start
extern kmain

start:
    mov [saved_magic], eax
    mov [saved_mbi], ebx
    mov esp, stack_top

    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x03
    mov [pml4], eax

    ; PDPT[0] -> PD
    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax

    ; PD[i]: 2 MiB страницы, identity map первых ~1 ГиБ
    mov edi, pd
    mov eax, 0x87        
    mov ecx, 512
.map_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map_pd

    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt_ptr]
    jmp CODE_SEL:long_mode

BITS 64
long_mode:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, stack_top
    mov edi, dword [saved_magic]
    mov esi, dword [saved_mbi]
    xor ebp, ebp
    call kmain
.hang:
    cli
    hlt
    jmp .hang
