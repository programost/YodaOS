; =============================================
; YodaOS 3.0 - Загрузчик
; =============================================
[bits 16]
[org 0x7C00]

start:
    ; Настройка сегментов
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Сохраняем номер диска
    mov [boot_drive], dl

    ; Очистка экрана
    mov ax, 0x0003
    int 0x10

    ; Вывод приветствия
    mov si, msg_boot
    call print_string

    ; Сброс диска
    mov ah, 0x00
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Загрузка ядра (точно 48 сектора = 24KB)
    mov ah, 0x02
    mov al, 48          ; 48 сектора = 24KB (достаточно для ядра)
    mov ch, 0           ; Цилиндр 0
    mov dh, 0           ; Головка 0
    mov cl, 2           ; Сектор 2 (после загрузчика)
    mov dl, [boot_drive]
    mov bx, 0x7E00      ; Адрес загрузки
    int 0x13
    jc disk_error
    
    jmp 0x7E00

disk_error:
    mov si, msg_disk_error
    call print_string
    
    ; Выводим код ошибки AH
    mov al, ah
    call print_hex_byte
    
    jmp $

print_string:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

print_hex_byte:
    ; Печатает байт в AL в HEX
    pusha
    mov cx, 2
.print_nibble:
    rol al, 4
    mov bl, al
    and bl, 0x0F
    add bl, '0'
    cmp bl, '9'
    jbe .print_char
    add bl, 7
.print_char:
    mov al, bl
    call print_char
    loop .print_nibble
    popa
    ret

print_char:
    mov ah, 0x0E
    int 0x10
    ret

msg_boot db 'YodaOS Bootloader v3.0', 0x0D, 0x0A, 'Loading kernel...', 0x0D, 0x0A, 0
msg_disk_error db 'Disk error! Code: ', 0

boot_drive db 0

times 510-($-$$) db 0
dw 0xAA55