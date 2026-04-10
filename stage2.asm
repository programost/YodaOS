; stage2.asm - loads kernel from disk, switches to 32-bit protected mode
[bits 16]
[org 0x7E00]

%ifndef KERNEL_SECTORS
  %define KERNEL_SECTORS 128
%endif

KERNEL_LOAD_ADDR equ 0x100000   ; final kernel address (1 MiB)
TEMP_SEGMENT    equ 0x1000      ; temporary buffer segment (linear 0x10000)
TEMP_OFFSET     equ 0x0000
MAX_SECTORS_PER_READ equ 127

start:
    mov ax, 0x0003      ; очистка экрана, текстовый режим 80x25
    int 0x10

    ; Сброс контроллера ATA
    mov dx, 0x3F6
    mov al, 0x04
    out dx, al
    mov ecx, 100000
.delay_reset:
    loop .delay_reset
    mov al, 0x00
    out dx, al

    mov cx, 0xFFFF
.delay_ata:
    loop .delay_ata

    mov si, msg_loading
    call print

    mov word [total_sectors], KERNEL_SECTORS
    mov dword [current_lba], 2      ; kernel starts at LBA 2

    mov ax, TEMP_SEGMENT
    mov es, ax
    xor bx, bx

read_loop:
    cmp word [total_sectors], 0
    je read_done

    mov cx, [total_sectors]
    cmp cx, MAX_SECTORS_PER_READ
    jbe .no_clip
    mov cx, MAX_SECTORS_PER_READ
.no_clip:
    mov word [dap.sectors], cx
    mov word [dap.offset], bx
    mov word [dap.segment], es
    mov eax, [current_lba]
    mov [dap.lba_low], eax
    mov dword [dap.lba_high], 0

    ; 3 попытки чтения
    mov di, 3
.retry_read:
    mov ah, 0x42
    mov si, dap
    mov dl, 0x80
    int 0x13
    jnc .read_ok
    dec di
    jz disk_error
    ; Сброс контроллера и повтор
    mov dx, 0x3F6
    mov al, 0x04
    out dx, al
    mov al, 0x00
    out dx, al
    jmp .retry_read

.read_ok:
    sub word [total_sectors], cx
    movzx eax, cx
    add dword [current_lba], eax

    shl cx, 9          ; cx *= 512
    add bx, cx
    jnc .no_segment_overflow
    mov ax, es
    add ax, 0x1000
    mov es, ax
    xor bx, bx
.no_segment_overflow:
    jmp read_loop

read_done:
    mov si, msg_ok
    call print

    mov si, msg_switch_pm
    call print

    ; Switch to protected mode
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

[bits 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x200000

    mov esi, msg_pm_copy
    call print32

    ; Copy kernel from temporary buffer to 1 MiB
    mov esi, TEMP_SEGMENT * 16 + TEMP_OFFSET
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    mov esi, msg_done
    call print32

    mov esi, msg_jumping
    call print32

    call KERNEL_LOAD_ADDR
    cli
    hlt

print32:
    mov edi, 0xB8000
    mov ah, 0x0F
.loop:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .loop
.done:
    ret

[bits 16]
disk_error:
    mov si, msg_disk_error
    call print
    cli
    hlt

print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

msg_loading db "Loading kernel...", 0
msg_ok      db " OK", 13, 10, 0
msg_switch_pm db "Switching to protected mode...", 13, 10, 0
msg_pm_copy db "Copying in PM... ", 0
msg_done    db "Done", 13, 10, 0
msg_jumping db "Jumping to kernel...", 0
msg_disk_error db "Disk read error!", 0

total_sectors dw 0
current_lba   dd 0

dap:
    .size    db 0x10
    .reserved db 0
    .sectors dw 0
    .offset  dw 0
    .segment dw 0
    .lba_low dd 0
    .lba_high dd 0

; GDT
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0
gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512 - ($-$$) db 0