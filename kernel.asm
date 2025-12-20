; =============================================
; YodaOS Kernel 2.1 - Enhanced Edition
; =============================================
[bits 16]
[org 0x7E00]

jmp start

; ---------- Константы и данные ----------
ASCII_ART1 db "\#\     /#/    /##########\     |#######\       /###\      ",0x0D, 0X0A,
db            " \$\   /$/     |#|      |#|     |#|   |#|      /#/ \#\     ",0X0D, 0X0A,
db            "  \$\ /$/      |#|      |#|     |#|   |#|     /#/   \#\    ",0X0D, 0X0A,
db            "   \$$$/       |#|      |#|     |#|   |#|    /#/     \#\   ",0X0D, 0X0A,
db            "    |#|        |#|      |#|     |#|   |#|   /###########\  ",0X0D, 0X0A,
db            "    |#|        |#|      |#|     |#|   |#|   |#|       |#|  ",0X0D, 0X0A,
db            "    |#|        |#|      |#|     |#|   |#|   |#|       |#|  ",0X0D,0X0A,
db            "    |#|        \##########/     |######/    |#|       |#|  ",0X0D,0X0A, 0

WELCOME_MSG db 'YodaOS v2.1 Enhanced Edition', 0x0D, 0x0A
            db '"May the Source be with you"', 0x0D, 0x0A
            db 'Type "help" for commands', 0x0D, 0x0A
            db 'Type "vga 1" for 320x200 graphics', 0x0D, 0x0A, 0

PROMPT_DEFAULT db 'yoda> ', 0
PROMPT_ROOT db 'root# ', 0

; Названия команд
CMD_HELP db 'help', 0
CMD_CLEAR db 'clear', 0
CMD_MEMTEST db 'memtest', 0
CMD_SYSINFO db 'sysinfo', 0
CMD_SYSCPD db 'syscpd', 0
CMD_SHUTDOWN db 'shutdown', 0
CMD_REBOOT db 'reboot', 0
CMD_CHSC db 'chsc', 0
CMD_COLOR db 'color', 0
CMD_SAUR db 'saur', 0
CMD_RP db 'rp', 0
CMD_RS db 'rs', 0
CMD_DATE db 'date', 0
CMD_TIME db 'time', 0
CMD_BEEP db 'beep', 0
CMD_VGA db 'vga', 0
CMD_GRAPHICS db 'graphics', 0
CMD_TEXT db 'text', 0
CMD_CALC db 'calc', 0
CMD_EDIT db 'edit', 0
CMD_DRAW db 'draw', 0
CMD_RAND db 'rand', 0
CMD_ECHO db 'echo', 0
CMD_PAUSE db 'pause', 0
CMD_CLS db 'cls', 0
CMD_TYPE db 'type', 0
CMD_MOUSE db 'mouse', 0
CMD_SOUND db 'sound', 0
CMD_VER db 'ver', 0
CMD_TEST db 'test', 0
CMD_DELAY db 'delay', 0
CMD_FILL db 'fill', 0
CMD_LINE db 'line', 0
CMD_CIRCLE db 'circle', 0

MSG_UNKNOWN db 'Unknown command', 0x0D, 0x0A, 0
MSG_NEWLINE db 0x0D, 0x0A, 0
MSG_PRESS_ANY_KEY db 'Press any key...', 0x0D, 0x0A, 0
MSG_CALC_RESULT db 'Result: ', 0
MSG_RANDOM_NUM db 'Random: ', 0
MSG_MOUSE_NOT_FOUND db 'Mouse not found', 0x0D, 0x0A, 0
MSG_MOUSE_FOUND db 'Mouse found. X:', 0
MSG_MOUSE_Y db ' Y:', 0
MSG_MOUSE_BUTTONS db ' Buttons:', 0
MSG_EDIT_HELP db 'Line editor. Use:', 0x0D, 0x0A
              db 'Ctrl+S: Save line', 0x0D, 0x0A
              db 'Ctrl+C: Clear line', 0x0D, 0x0A
              db 'Ctrl+X: Exit', 0x0D, 0x0A
              db 'Line saved to memory buffer', 0x0D, 0x0A, 0
MSG_ECHO_HELP db 'Echo text to screen', 0x0D, 0x0A, 0
MSG_VERSION db 'YodaOS v2.1 Enhanced Edition', 0x0D, 0x0A
            db 'Build: 1995-12-24 18:30:00', 0x0D, 0x0A, 0

; ---------- Переменные в RAM ----------
cmd_buffer times 128 db 0
cmd_name times 32 db 0
cmd_args times 96 db 0
current_prompt dd PROMPT_DEFAULT
root_password db 'yoda123', 0
is_root db 0
current_color db 0x07
selected_ascii db 1
custom_cursor times 16 db 'yoda> ', 0
video_mode db 0x03  ; 0x03 = текстовый, 0x13 = графический
cursor_x db 0
cursor_y db 0
calc_buffer times 64 db 0
edit_buffer times 256 db 0
random_seed dw 0x1234
mouse_x dw 0
mouse_y dw 0
mouse_buttons db 0
line_x1 dw 0
line_y1 dw 0
line_x2 dw 0
line_y2 dw 0
circle_x dw 0
circle_y dw 0
circle_radius dw 0
temp_var dw 0
graphics_x dw 0
graphics_y dw 0
char_x dw 0
char_y dw 0

; ---------- Старт ядра ----------
start:
    ; Инициализация
    call clear_screen
    call show_ascii_art
    mov si, WELCOME_MSG
    call print_string
    
    ; Инициализация генератора случайных чисел (используем таймер)
    mov ah, 0x00
    int 0x1A
    mov [random_seed], dx

main_loop:
    ; Показываем промпт
    mov si, [current_prompt]
    call print_string
    
    ; Читаем команду
    mov di, cmd_buffer
    call read_string
    
    ; Разбираем команду
    call parse_cmd_line
    
    ; Выполняем команду
    call execute_command
    jmp main_loop

; ---------- Графические функции ----------
init_graphics:
    ; Устанавливаем видеорежим 0x13 (320x200, 256 цветов)
    mov ax, 0x0013
    int 0x10
    mov byte [video_mode], 0x13
    mov word [graphics_x], 0
    mov word [graphics_y], 0
    mov word [char_x], 0
    mov word [char_y], 0
    ret

set_text_mode:
    ; Возврат в текстовый режим 0x03
    mov ax, 0x0003
    int 0x10
    mov byte [video_mode], 0x03
    mov word [graphics_x], 0
    mov word [graphics_y], 0
    mov word [char_x], 0
    mov word [char_y], 0
    ret

; Функция для отображения пикселя в режиме 0x13
; CX = X координата (0-319), DX = Y координата (0-199), AL = цвет (0-255)
draw_pixel:
    pusha
    mov ah, 0x0C
    mov bh, 0
    int 0x10
    popa
    ret

; Функция для отображения символа в графическом режиме
; AL = символ, BL = цвет, CX = X, DX = Y
draw_char_graphics:
    pusha
    
    ; Сохраняем параметры
    mov [.char], al
    mov [.color], bl
    mov [.x], cx
    mov [.y], dx
    
    ; Получаем указатель на шрифт BIOS (8x16)
    push es
    push ds
    push bp
    
    mov ax, 0x1130
    mov bh, 0x06  ; 8x16 шрифт
    int 0x10      ; ES:BP теперь указывает на таблицу шрифтов
    
    ; Сохраняем указатель на шрифт
    mov [.font_ptr], bp
    mov [.font_seg], es
    
    pop bp
    pop ds
    
    ; Вычисляем смещение символа в таблице шрифтов
    movzx ax, byte [.char]
    shl ax, 4      ; Каждый символ = 16 байт
    mov si, ax
    
    ; Устанавливаем сегмент шрифта
    mov ax, [.font_seg]
    mov fs, ax
    
    ; Рисуем символ
    mov cx, [.x]
    mov dx, [.y]
    
    mov bh, 0      ; Страница
    mov bl, [.color]
    
    mov di, 0      ; Счетчик строк
.char_loop:
    mov al, byte [fs:si]  ; Байт из шрифта
    inc si
    
    mov ah, 8      ; 8 бит в строке
.bit_loop:
    test al, 0x80  ; Проверяем старший бит
    jz .skip_pixel
    
    ; Рисуем пиксель
    push ax
    push cx
    push dx
    mov al, bl
    call draw_pixel
    pop dx
    pop cx
    pop ax
    
.skip_pixel:
    shl al, 1      ; Сдвигаем влево
    inc cx         ; Следующий пиксель по X
    
    dec ah
    jnz .bit_loop
    
    ; Переход к следующей строке
    mov cx, [.x]
    inc dx
    inc di
    
    cmp di, 16     ; 16 строк на символ
    jl .char_loop
    
    pop es
    popa
    ret
    
.char db 0
.color db 0
.x dw 0
.y dw 0
.font_ptr dw 0
.font_seg dw 0

; Функция для отображения строки в графическом режиме
; SI = строка, BL = цвет, CX = X, DX = Y
draw_string_graphics:
    pusha
    
.string_loop:
    lodsb
    test al, al
    jz .done
    
    ; Пропускаем управляющие символы
    cmp al, 0x0D   ; CR
    je .next_char
    cmp al, 0x0A   ; LF
    je .next_char
    
    ; Рисуем символ
    call draw_char_graphics
    
    ; Смещаемся для следующего символа (8 пикселей ширина + 1 промежуток)
    add cx, 9
    
.next_char:
    jmp .string_loop
    
.done:
    popa
    ret

; НОВАЯ: Рисование линии (алгоритм Брезенхема)
; CX = x1, DX = y1, SI = x2, DI = y2, AL = цвет
draw_line:
    pusha
    
    mov [.x1], cx
    mov [.y1], dx
    mov [.x2], si
    mov [.y2], di
    mov [.color], al
    
    ; Вычисляем dx и dy
    mov ax, si
    sub ax, cx
    mov [.dx], ax
    mov bx, ax
    
    mov ax, di
    sub ax, dx
    mov [.dy], ax
    
    ; Определяем направление
    cmp word [.dx], 0
    jge .dx_positive
    neg word [.dx]
    mov bx, -1
    jmp .dx_done
.dx_positive:
    mov bx, 1
.dx_done:
    mov [.sx], bx
    
    cmp word [.dy], 0
    jge .dy_positive
    neg word [.dy]
    mov bx, -1
    jmp .dy_done
.dy_positive:
    mov bx, 1
.dy_done:
    mov [.sy], bx
    
    ; Сравниваем dx и dy
    mov ax, [.dx]
    cmp ax, [.dy]
    jge .dx_ge_dy
    
    ; dy > dx
    mov ax, [.dy]
    neg ax
    mov [.err], ax
    
.loop1:
    ; Рисуем пиксель
    mov cx, [.x1]
    mov dx, [.y1]
    mov al, [.color]
    call draw_pixel
    
    ; Проверяем конец линии
    mov ax, [.x1]
    cmp ax, [.x2]
    jne .continue1
    mov ax, [.y1]
    cmp ax, [.y2]
    je .done
    
.continue1:
    mov ax, [.err]
    add ax, [.dx]
    mov [.err], ax
    
    cmp ax, 0
    jle .no_x1
    mov ax, [.x1]
    add ax, [.sx]
    mov [.x1], ax
    mov ax, [.err]
    sub ax, [.dy]
    mov [.err], ax
    
.no_x1:
    mov ax, [.y1]
    add ax, [.sy]
    mov [.y1], ax
    jmp .loop1
    
.dx_ge_dy:
    ; dx >= dy
    mov ax, [.dx]
    mov [.err], ax
    
.loop2:
    ; Рисуем пиксель
    mov cx, [.x1]
    mov dx, [.y1]
    mov al, [.color]
    call draw_pixel
    
    ; Проверяем конец линии
    mov ax, [.x1]
    cmp ax, [.x2]
    jne .continue2
    mov ax, [.y1]
    cmp ax, [.y2]
    je .done
    
.continue2:
    mov ax, [.err]
    sub ax, [.dy]
    mov [.err], ax
    
    cmp ax, 0
    jge .no_y2
    mov ax, [.y1]
    add ax, [.sy]
    mov [.y1], ax
    mov ax, [.err]
    add ax, [.dx]
    mov [.err], ax
    
.no_y2:
    mov ax, [.x1]
    add ax, [.sx]
    mov [.x1], ax
    jmp .loop2
    
.done:
    popa
    ret
    
.x1 dw 0
.y1 dw 0
.x2 dw 0
.y2 dw 0
.dx dw 0
.dy dw 0
.sx dw 0
.sy dw 0
.err dw 0
.color db 0

; НОВАЯ: Рисование окружности
; CX = x, DX = y, SI = радиус, AL = цвет
draw_circle:
    pusha
    
    mov [.cx], cx
    mov [.cy], dx
    mov [.r], si
    mov [.color], al
    
    mov word [.x], 0
    mov ax, si
    mov [.y], ax
    
    mov ax, 1
    sub ax, si
    mov [.d], ax
    
.while:
    mov ax, [.x]
    cmp ax, [.y]
    jg .done
    
    ; Рисуем 8 симметричных точек
    mov cx, [.cx]
    mov dx, [.cy]
    add cx, [.x]
    add dx, [.y]
    mov al, [.color]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    sub cx, [.x]
    add dx, [.y]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    add cx, [.x]
    sub dx, [.y]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    sub cx, [.x]
    sub dx, [.y]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    add cx, [.y]
    add dx, [.x]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    sub cx, [.y]
    add dx, [.x]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    add cx, [.y]
    sub dx, [.x]
    call draw_pixel
    
    mov cx, [.cx]
    mov dx, [.cy]
    sub cx, [.y]
    sub dx, [.x]
    call draw_pixel
    
    ; Обновляем переменные
    mov ax, [.d]
    cmp ax, 0
    jg .d_gt_0
    
    ; d <= 0
    mov ax, [.x]
    shl ax, 1
    add ax, 3
    add [.d], ax
    jmp .update_x
    
.d_gt_0:
    ; d > 0
    mov ax, [.x]
    sub ax, [.y]
    shl ax, 1
    add ax, 5
    add [.d], ax
    dec word [.y]
    
.update_x:
    inc word [.x]
    jmp .while
    
.done:
    popa
    ret
    
.cx dw 0
.cy dw 0
.r dw 0
.x dw 0
.y dw 0
.d dw 0
.color db 0

; НОВАЯ: Заполнение области цветом
; CX = x, DX = y, SI = ширина, DI = высота, AL = цвет
fill_rect:
    pusha
    
    mov [.x], cx
    mov [.y], dx
    mov [.width], si
    mov [.height], di
    mov [.color], al
    
.y_loop:
    mov cx, [.width]
    mov bx, [.x]
    
.x_loop:
    push cx
    mov cx, bx
    mov dx, [.y]
    mov al, [.color]
    call draw_pixel
    inc bx
    pop cx
    loop .x_loop
    
    inc word [.y]
    dec word [.height]
    jnz .y_loop
    
    popa
    ret
    
.x dw 0
.y dw 0
.width dw 0
.height dw 0
.color db 0

; Демонстрация графики
demo_graphics:
    pusha
    
    ; Очищаем экран черным
    mov ax, 0xA000
    mov es, ax
    xor di, di
    mov cx, 32000
    xor ax, ax
    rep stosw
    
    ; Рисуем градиент
    mov cx, 0      ; X
.gradient_x:
    mov dx, 0      ; Y
.gradient_y:
    mov al, dl     ; Цвет = Y координата
    call draw_pixel
    inc dx
    cmp dx, 200
    jl .gradient_y
    inc cx
    cmp cx, 320
    jl .gradient_x
    
    ; Рисуем текст
    mov si, graphics_msg
    mov bl, 15     ; Белый цвет
    mov cx, 100    ; X
    mov dx, 80     ; Y
    call draw_string_graphics
    
    ; Рисуем цветные квадраты
    mov al, 4      ; Красный
    mov cx, 50
    mov dx, 30
    call draw_square
    
    mov al, 2      ; Зеленый
    mov cx, 100
    mov dx, 30
    call draw_square
    
    mov al, 1      ; Синий
    mov cx, 150
    mov dx, 30
    call draw_square
    
    mov al, 14     ; Желтый
    mov cx, 200
    mov dx, 30
    call draw_square
    
    ; Ждем нажатия клавиши
    call wait_for_key
    
    popa
    ret

draw_square:
    ; Рисует квадрат 20x20
    ; AL = цвет, CX = X, DX = Y
    pusha
    
    mov [.color], al
    mov [.start_x], cx
    mov [.start_y], dx
    
    mov dx, [.start_y]
    mov cx, 20      ; Высота
.y_loop:
    push cx
    mov cx, [.start_x]
    mov bx, 20      ; Ширина
    
.x_loop:
    push cx
    push dx
    mov al, [.color]
    call draw_pixel
    pop dx
    pop cx
    inc cx
    dec bx
    jnz .x_loop
    
    inc dx
    pop cx
    loop .y_loop
    
    popa
    ret
    
.color db 0
.start_x dw 0
.start_y dw 0

wait_for_key:
    ; Ждет нажатия любой клавиши
    pusha
    xor ah, ah
    int 0x16
    popa
    ret

; ---------- Основные функции ----------
print_char:
    cmp byte [video_mode], 0x13
    je .graphics_mode
    
    ; Текстовый режим
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    ret
    
.graphics_mode:
    ; Графический режим
    pusha
    mov bl, 15  ; Белый цвет
    mov cx, [char_x]
    mov dx, [char_y]
    call draw_char_graphics
    
    ; Обновляем позицию курсора
    inc word [char_x]
    cmp word [char_x], 320
    jl .done_graphics
    
    ; Перенос строки
    mov word [char_x], 0
    add word [char_y], 16
    
.done_graphics:
    popa
    ret

print_string:
    cmp byte [video_mode], 0x13
    je .graphics_mode
    
    ; Текстовый режим
    push si
.text_loop:
    lodsb
    test al, al
    jz .text_done
    call print_char
    jmp .text_loop
.text_done:
    pop si
    ret
    
.graphics_mode:
    ; Графический режим
    pusha
    mov bl, 15  ; Белый цвет
    mov cx, [graphics_x]
    mov dx, [graphics_y]
    
.graphics_loop:
    lodsb
    test al, al
    jz .graphics_done
    
    ; Обработка переноса строк
    cmp al, 0x0D
    je .carriage_return
    cmp al, 0x0A
    je .line_feed
    
    call draw_char_graphics
    add cx, 9
    
    jmp .graphics_loop
    
.carriage_return:
    mov cx, [graphics_x]
    jmp .graphics_loop
    
.line_feed:
    add dx, 16
    mov cx, [graphics_x]
    jmp .graphics_loop
    
.graphics_done:
    ; Сохраняем позицию
    mov [graphics_x], cx
    mov [graphics_y], dx
    popa
    ret

read_char:
    xor ah, ah
    int 0x16
    ret

read_string:
    push di
    xor cx, cx
.read_loop:
    call read_char
    cmp al, 0x0D
    je .read_done
    cmp al, 0x08
    je .read_backspace
    
    stosb
    inc cx
    call print_char
    jmp .read_loop
    
.read_backspace:
    test cx, cx
    jz .read_loop
    dec di
    dec cx
    
    mov al, 0x08
    call print_char
    mov al, ' '
    call print_char
    mov al, 0x08
    call print_char
    jmp .read_loop
    
.read_done:
    mov byte [di], 0
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char
    pop di
    ret

clear_screen:
    cmp byte [video_mode], 0x13
    je .graphics_clear
    
    ; Текстовый режим
    mov ax, 0x0003
    int 0x10
    
    ; Сброс позиции курсора в графическом режиме
    mov word [graphics_x], 0
    mov word [graphics_y], 0
    mov word [char_x], 0
    mov word [char_y], 0
    
    ret
    
.graphics_clear:
    ; Графический режим
    pusha
    mov ax, 0xA000
    mov es, ax
    xor di, di
    mov cx, 32000
    xor ax, ax
    rep stosw
    
    ; Сброс позиции курсора
    mov word [graphics_x], 0
    mov word [graphics_y], 0
    mov word [char_x], 0
    mov word [char_y], 0
    
    popa
    ret

show_ascii_art:
    ; Показываем ASCII art только в текстовом режиме
    cmp byte [video_mode], 0x03
    jne .skip_art
    
    mov si, ASCII_ART1
    call print_string
    
.skip_art:
    ret

; ---------- Функции для работы со строками ----------
strcmp:
    push si
    push di
.strcmp_loop:
    mov al, [si]
    mov bl, [di]
    cmp al, bl
    jne .strcmp_not_equal
    test al, al
    jz .strcmp_equal
    inc si
    inc di
    jmp .strcmp_loop
.strcmp_not_equal:
    pop di
    pop si
    test al, al
    ret
.strcmp_equal:
    pop di
    pop si
    xor ax, ax
    ret

; ---------- Парсинг командной строки ----------
parse_cmd_line:
    mov si, cmd_buffer
    mov di, cmd_name
    
.parse_skip_spaces:
    lodsb
    test al, al
    jz .parse_empty
    cmp al, ' '
    je .parse_skip_spaces
    
    dec si
.parse_copy_name:
    lodsb
    test al, al
    jz .parse_no_args
    cmp al, ' '
    je .parse_name_done
    stosb
    jmp .parse_copy_name
    
.parse_name_done:
    mov byte [di], 0
    
    mov di, cmd_args
.parse_copy_args:
    lodsb
    test al, al
    jz .parse_args_done
    cmp al, ' '
    jbe .parse_copy_args
    
    dec si
.parse_copy_args_loop:
    lodsb
    test al, al
    jz .parse_args_done
    stosb
    jmp .parse_copy_args_loop
    
.parse_args_done:
    mov byte [di], 0
    ret
    
.parse_no_args:
    mov byte [di], 0
    mov byte [cmd_args], 0
    ret
    
.parse_empty:
    mov byte [cmd_name], 0
    mov byte [cmd_args], 0
    ret

; ---------- Вспомогательные функции ----------
generate_random:
    ; Генерация случайного числа (0-255 в AL)
    push bx
    push cx
    push dx
    
    mov ax, [random_seed]
    mov bx, 1103515245
    mul bx
    add ax, 12345
    mov [random_seed], ax
    
    ; Используем таймер как дополнительный источник энтропии
    mov ah, 0x00
    int 0x1A
    xor ax, dx
    
    pop dx
    pop cx
    pop bx
    ret

print_decimal:
    ; Печатает число в AX
    pusha
    mov bx, 10
    xor cx, cx
    
.print_decimal_divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .print_decimal_divide
    
.print_decimal_print:
    pop ax
    add al, '0'
    call print_char
    loop .print_decimal_print
    popa
    ret

print_bcd:
    ; Печатает BCD число в AL
    push ax
    shr al, 4
    add al, '0'
    call print_char
    pop ax
    and al, 0x0F
    add al, '0'
    call print_char
    ret

; ---------- Выполнение команды ----------
execute_command:
    mov al, [cmd_name]
    test al, al
    jz .execute_done
    
    ; help
    mov si, cmd_name
    mov di, CMD_HELP
    call strcmp
    jz do_help
    
    ; clear
    mov si, cmd_name
    mov di, CMD_CLEAR
    call strcmp
    jz do_clear
    
    ; memtest
    mov si, cmd_name
    mov di, CMD_MEMTEST
    call strcmp
    jz do_memtest
    
    ; sysinfo
    mov si, cmd_name
    mov di, CMD_SYSINFO
    call strcmp
    jz do_sysinfo
    
    ; syscpd
    mov si, cmd_name
    mov di, CMD_SYSCPD
    call strcmp
    jz do_syscpd
    
    ; shutdown
    mov si, cmd_name
    mov di, CMD_SHUTDOWN
    call strcmp
    jz do_shutdown
    
    ; reboot
    mov si, cmd_name
    mov di, CMD_REBOOT
    call strcmp
    jz do_reboot
    
    ; chsc
    mov si, cmd_name
    mov di, CMD_CHSC
    call strcmp
    jz do_chsc
    
    ; color
    mov si, cmd_name
    mov di, CMD_COLOR
    call strcmp
    jz do_color
    
    ; saur
    mov si, cmd_name
    mov di, CMD_SAUR
    call strcmp
    jz do_saur
    
    ; rp
    mov si, cmd_name
    mov di, CMD_RP
    call strcmp
    jz do_rp
    
    ; rs
    mov si, cmd_name
    mov di, CMD_RS
    call strcmp
    jz do_rs
    
    ; date
    mov si, cmd_name
    mov di, CMD_DATE
    call strcmp
    jz do_date
    
    ; time
    mov si, cmd_name
    mov di, CMD_TIME
    call strcmp
    jz do_time
    
    ; beep
    mov si, cmd_name
    mov di, CMD_BEEP
    call strcmp
    jz do_beep
    
    ; vga
    mov si, cmd_name
    mov di, CMD_VGA
    call strcmp
    jz do_vga
    
    ; graphics
    mov si, cmd_name
    mov di, CMD_GRAPHICS
    call strcmp
    jz do_graphics
    
    ; text
    mov si, cmd_name
    mov di, CMD_TEXT
    call strcmp
    jz do_text
    
    ; calc
    mov si, cmd_name
    mov di, CMD_CALC
    call strcmp
    jz do_calc
    
    ; edit
    mov si, cmd_name
    mov di, CMD_EDIT
    call strcmp
    jz do_edit
    
    ; draw
    mov si, cmd_name
    mov di, CMD_DRAW
    call strcmp
    jz do_draw
    
    ; rand
    mov si, cmd_name
    mov di, CMD_RAND
    call strcmp
    jz do_rand
    
    ; echo
    mov si, cmd_name
    mov di, CMD_ECHO
    call strcmp
    jz do_echo
    
    ; pause
    mov si, cmd_name
    mov di, CMD_PAUSE
    call strcmp
    jz do_pause
    
    ; cls
    mov si, cmd_name
    mov di, CMD_CLS
    call strcmp
    jz do_clear
    
    ; type
    mov si, cmd_name
    mov di, CMD_TYPE
    call strcmp
    jz do_type
    
    ; mouse
    mov si, cmd_name
    mov di, CMD_MOUSE
    call strcmp
    jz do_mouse
    
    ; sound
    mov si, cmd_name
    mov di, CMD_SOUND
    call strcmp
    jz do_sound
    
    ; ver
    mov si, cmd_name
    mov di, CMD_VER
    call strcmp
    jz do_ver
    
    ; test
    mov si, cmd_name
    mov di, CMD_TEST
    call strcmp
    jz do_test
    
    ; delay
    mov si, cmd_name
    mov di, CMD_DELAY
    call strcmp
    jz do_delay
    
    ; fill
    mov si, cmd_name
    mov di, CMD_FILL
    call strcmp
    jz do_fill
    
    ; line
    mov si, cmd_name
    mov di, CMD_LINE
    call strcmp
    jz do_line
    
    ; circle
    mov si, cmd_name
    mov di, CMD_CIRCLE
    call strcmp
    jz do_circle
    
    ; Неизвестная команда
    mov si, MSG_UNKNOWN
    call print_string
    
.execute_done:
    ret

; ---------- Существующие команды ----------
do_help:
    mov si, help_text
    call print_string
    ret

do_clear:
    call clear_screen
    call show_ascii_art
    ret

do_memtest:
    mov si, memtest_msg1
    call print_string
    
    mov ah, 0x88
    int 0x15
    jc .memtest_error
    
    mov si, memtest_msg2
    call print_string
    
    call print_decimal
    
    mov si, memtest_msg3
    call print_string
    ret
    
.memtest_error:
    mov si, memtest_error
    call print_string
    ret

do_sysinfo:
    mov si, cmd_args
    cmp byte [si], 0
    je .sysinfo_show_all
    
    cmp byte [si], '-'
    jne .sysinfo_show_all
    
    inc si
    mov al, [si]
    
    cmp al, 'c'
    je .sysinfo_cpu_only
    cmp al, 'v'
    je .sysinfo_video_only
    cmp al, 'a'
    je .sysinfo_show_all
    
.sysinfo_show_all:
    mov si, cpu_info
    call print_string
    mov si, video_info
    call print_string
    mov si, memory_info
    call print_string
    ret
    
.sysinfo_cpu_only:
    mov si, cpu_info
    call print_string
    ret
    
.sysinfo_video_only:
    mov si, video_info
    call print_string
    ret

do_syscpd:
    mov si, syscpd_msg
    call print_string
    ret

do_shutdown:
    mov si, cmd_args
    cmp byte [si], 0
    je .shutdown_now
    
    cmp byte [si], '-'
    jne .shutdown_now
    
    inc si
    mov al, [si]
    
    cmp al, 't'
    je .shutdown_timed
    cmp al, 'r'
    je .shutdown_reboot
    
.shutdown_now:
    mov si, shutdown_msg
    call print_string
    
    mov ax, 0x5307
    mov bx, 0x0001
    mov cx, 0x0003
    int 0x15
    
    cli
    hlt
    
.shutdown_timed:
    mov si, shutdown_timed
    call print_string
    
    mov cx, 180
.shutdown_delay:
    push cx
    mov cx, 0xFFFF
.shutdown_wait:
    loop .shutdown_wait
    pop cx
    loop .shutdown_delay
    
    jmp .shutdown_now
    
.shutdown_reboot:
    jmp do_reboot

do_reboot:
    mov si, reboot_msg
    call print_string
    
    mov cx, 0xFFFF
.reboot_delay:
    loop .reboot_delay
    
    jmp 0xFFFF:0x0000

do_chsc:
    mov si, cmd_args
    cmp byte [si], 0
    je .chsc_show_all
    
    cmp byte [si], '-'
    jne .chsc_show_all
    
    inc si
    mov al, [si]
    
    cmp al, 'c'
    je .chsc_components
    cmp al, 'b'
    je .chsc_boot
    cmp al, 'k'
    je .chsc_kernel
    cmp al, 'a'
    je .chsc_show_all
    
.chsc_show_all:
    mov si, chsc_all
    call print_string
    ret
    
.chsc_components:
    mov si, chsc_components
    call print_string
    ret
    
.chsc_boot:
    mov si, chsc_boot
    call print_string
    ret
    
.chsc_kernel:
    mov si, chsc_kernel
    call print_string
    ret

do_color:
    mov si, cmd_args
    cmp byte [si], 0
    je .color_show_current
    
    mov al, [si]
    sub al, '0'
    
    cmp al, 0
    jb .color_error
    cmp al, 15
    ja .color_error
    
    mov [current_color], al
    
    mov ah, 0x0B
    mov bh, 0
    mov bl, al
    int 0x10
    
    mov si, color_changed
    call print_string
    ret
    
.color_show_current:
    mov si, current_color_msg
    call print_string
    mov al, [current_color]
    xor ah, ah
    call print_decimal
    mov si, MSG_NEWLINE
    call print_string
    ret
    
.color_error:
    mov si, color_error
    call print_string
    ret

do_saur:
    mov si, cmd_args
    cmp byte [si], 0
    je .saur_check_status
    
    cmp byte [si], '-'
    jne .saur_check_status
    
    inc si
    mov al, [si]
    
    cmp al, 'c'
    je .saur_check_status
    cmp al, 'e'
    je .saur_enter_password
    cmp al, 'x'
    je .saur_exit_root
    cmp al, 'r'
    je .saur_enter_root
    
    jmp .saur_error
    
.saur_check_status:
    mov al, [is_root]
    test al, al
    jnz .saur_is_root
    
    mov si, saur_user
    call print_string
    ret
    
.saur_is_root:
    mov si, saur_root
    call print_string
    ret
    
.saur_enter_password:
    mov si, password_prompt
    call print_string
    
    mov di, cmd_buffer
    xor cx, cx
.saur_read_pass:
    call read_char
    cmp al, 0x0D
    je .saur_check_password
    
    stosb
    inc cx
    
    mov al, '*'
    call print_char
    jmp .saur_read_pass
    
.saur_check_password:
    mov byte [di], 0
    
    mov si, cmd_buffer
    mov di, root_password
    call strcmp
    jz .saur_password_correct
    
    mov si, password_wrong
    call print_string
    ret
    
.saur_password_correct:
    mov si, password_correct_msg
    call print_string
    ret
    
.saur_exit_root:
    mov byte [is_root], 0
    mov dword [current_prompt], PROMPT_DEFAULT
    mov si, exit_root_msg
    call print_string
    ret
    
.saur_enter_root:
    mov al, [is_root]
    test al, al
    jnz .saur_already_root
    
    mov byte [is_root], 1
    mov dword [current_prompt], PROMPT_ROOT
    mov si, enter_root_msg
    call print_string
    ret
    
.saur_already_root:
    mov si, already_root_msg
    call print_string
    ret
    
.saur_error:
    mov si, saur_error
    call print_string
    ret

do_rp:
    mov si, cmd_args
    cmp byte [si], '-'
    jne .rp_error
    
    inc si
    mov ax, [si]
    cmp ax, 'ch'
    jne .rp_error
    
    add si, 7
    
    mov di, cmd_buffer
.rp_copy_old:
    lodsb
    cmp al, ' '
    je .rp_got_old
    test al, al
    jz .rp_error
    stosb
    jmp .rp_copy_old
    
.rp_got_old:
    mov byte [di], 0
    
    mov si, cmd_buffer
    mov di, root_password
    call strcmp
    jz .rp_password_ok
    
    mov si, password_wrong
    call print_string
    ret
    
.rp_password_ok:
    mov di, root_password
.rp_copy_new:
    lodsb
    test al, al
    jz .rp_done_copy
    stosb
    jmp .rp_copy_new
    
.rp_done_copy:
    mov byte [di], 0
    mov si, password_changed
    call print_string
    ret
    
.rp_error:
    mov si, rp_usage
    call print_string
    ret

do_rs:
    mov si, cmd_args
    cmp byte [si], '-'
    jne .rs_error
    
    inc si
    mov al, [si]
    
    cmp al, 'c'
    je .rs_cursor
    cmp al, 'a'
    je .rs_asciiart
    
.rs_error:
    mov si, rs_usage
    call print_string
    ret
    
.rs_cursor:
    add si, 7
    
    mov di, custom_cursor
.rs_copy_cursor:
    lodsb
    test al, al
    jz .rs_cursor_done
    stosb
    jmp .rs_copy_cursor
    
.rs_cursor_done:
    mov byte [di], 0
    
    mov al, [is_root]
    test al, al
    jz .rs_not_root
    
    mov dword [current_prompt], custom_cursor
    
.rs_not_root:
    mov si, cursor_changed
    call print_string
    ret
    
.rs_asciiart:
    add si, 9
    
    mov al, [si]
    sub al, '0'
    
    cmp al, 1
    jb .rs_ascii_error
    cmp al, 3
    ja .rs_ascii_error
    
    mov [selected_ascii], al
    
    mov si, asciiart_changed
    call print_string
    call clear_screen
    call show_ascii_art
    ret
    
.rs_ascii_error:
    mov si, asciiart_error
    call print_string
    ret

do_date:
    ; Получаем дату от CMOS
    mov ah, 0x04
    int 0x1A
    jc .date_error
    
    ; День
    mov al, dl
    call print_bcd
    mov al, '/'
    call print_char
    
    ; Месяц
    mov al, dh
    call print_bcd
    mov al, '/'
    call print_char
    
    ; Год
    mov al, 0x20
    call print_char
    mov al, ch
    call print_bcd
    mov al, cl
    call print_bcd
    
    mov si, MSG_NEWLINE
    call print_string
    ret
    
.date_error:
    mov si, date_error
    call print_string
    ret

do_time:
    ; Получаем время от CMOS
    mov ah, 0x02
    int 0x1A
    jc .time_error
    
    ; Часы
    mov al, ch
    call print_bcd
    mov al, ':'
    call print_char
    
    ; Минуты
    mov al, cl
    call print_bcd
    mov al, ':'
    call print_char
    
    ; Секунды
    mov al, dh
    call print_bcd
    
    mov si, MSG_NEWLINE
    call print_string
    ret
    
.time_error:
    mov si, time_error
    call print_string
    ret

do_beep:
    ; Издает звуковой сигнал
    mov al, 0x07
    call print_char
    
    ; Используем PC speaker
    mov al, 0xB6
    out 0x43, al
    
    mov ax, 1193  ; Частота ~1000Hz
    out 0x42, al
    mov al, ah
    out 0x42, al
    
    in al, 0x61
    or al, 0x03
    out 0x61, al
    
    ; Задержка
    mov cx, 0x1FFF
.beep_delay:
    loop .beep_delay
    
    ; Выключаем звук
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    
    mov si, beep_done
    call print_string
    ret

do_vga:
    ; Переключает видеорежим
    mov si, cmd_args
    cmp byte [si], 0
    je .vga_show_mode
    
    cmp byte [si], '1'
    je .vga_mode13
    cmp byte [si], '3'
    je .vga_mode03
    
    jmp .vga_error
    
.vga_show_mode:
    mov si, current_mode
    call print_string
    mov al, [video_mode]
    xor ah, ah
    call print_decimal
    mov si, MSG_NEWLINE
    call print_string
    ret
    
.vga_mode13:
    call init_graphics
    mov si, mode13_set
    call print_string
    
    ; Показываем демо графики
    call demo_graphics
    
    ; Возвращаемся в текстовый режим
    call set_text_mode
    ret
    
.vga_mode03:
    call set_text_mode
    mov si, mode03_set
    call print_string
    ret
    
.vga_error:
    mov si, vga_error
    call print_string
    ret

do_graphics:
    ; Переключает в графический режим и показывает демо
    call init_graphics
    call demo_graphics
    call set_text_mode
    ret

do_text:
    ; Переключает в текстовый режим
    call set_text_mode
    mov si, text_mode_set
    call print_string
    ret

; ---------- Новые команды ----------
do_calc:
    ; Простой калькулятор
    pusha
    
    mov si, cmd_args
    cmp byte [si], 0
    je .calc_help
    
    ; Пока просто эхо для демонстрации
    mov si, MSG_CALC_RESULT
    call print_string
    mov si, cmd_args
    call print_string
    mov si, MSG_NEWLINE
    call print_string
    
    popa
    ret
    
.calc_help:
    mov si, .calc_help_msg
    call print_string
    popa
    ret
    
.calc_help_msg db 'Usage: calc <expression>', 0x0D, 0x0A
               db 'Example: calc 10+20', 0x0D, 0x0A, 0

do_edit:
    ; Простой построчный редактор
    pusha
    
    mov si, MSG_EDIT_HELP
    call print_string
    
    mov di, edit_buffer
    xor cx, cx
    
.edit_loop:
    call read_char
    
    cmp al, 0x0D  ; Enter
    je .new_line
    
    cmp al, 0x08  ; Backspace
    je .backspace
    
    cmp al, 0x13  ; Ctrl+S (сохранить)
    je .save_line
    
    cmp al, 0x03  ; Ctrl+C (очистить)
    je .clear_line
    
    cmp al, 0x18  ; Ctrl+X (выход)
    je .exit_editor
    
    ; Проверяем буфер
    cmp cx, 255
    jge .edit_loop
    
    ; Сохраняем символ
    stosb
    inc cx
    
    ; Отображаем символ
    call print_char
    jmp .edit_loop
    
.new_line:
    mov byte [di], 0
    mov si, edit_buffer
    call print_string
    mov si, MSG_NEWLINE
    call print_string
    
    mov di, edit_buffer
    xor cx, cx
    jmp .edit_loop
    
.backspace:
    test cx, cx
    jz .edit_loop
    
    dec di
    dec cx
    
    mov al, 0x08
    call print_char
    mov al, ' '
    call print_char
    mov al, 0x08
    call print_char
    jmp .edit_loop
    
.save_line:
    mov byte [di], 0
    mov si, .saved_msg
    call print_string
    jmp .edit_loop
    
.clear_line:
    mov di, edit_buffer
    xor cx, cx
    
    ; Очищаем строку на экране
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char
    jmp .edit_loop
    
.exit_editor:
    popa
    ret
    
.saved_msg db 'Line saved to buffer.', 0x0D, 0x0A, 0

do_draw:
    ; Графическая демонстрация
    call init_graphics
    
    ; Рисуем несколько фигур
    mov al, 4      ; Красный
    mov cx, 50
    mov dx, 50
    mov si, 100
    mov di, 80
    call fill_rect
    
    mov al, 2      ; Зеленый
    mov cx, 200
    mov dx, 80
    mov si, 30
    call draw_circle
    
    mov al, 14     ; Желтый
    mov cx, 100
    mov dx, 150
    mov si, 180
    mov di, 180
    call draw_line
    
    ; Выводим текст
    mov si, .draw_msg
    mov bl, 15
    mov cx, 10
    mov dx, 180
    call draw_string_graphics
    
    call wait_for_key
    call set_text_mode
    ret
    
.draw_msg db 'Graphics Demo - Press any key', 0

do_rand:
    ; Генерация случайного числа
    call generate_random
    push ax
    
    mov si, MSG_RANDOM_NUM
    call print_string
    
    pop ax
    xor ah, ah
    call print_decimal
    
    mov si, MSG_NEWLINE
    call print_string
    ret

do_echo:
    ; Вывод текста
    mov si, cmd_args
    cmp byte [si], 0
    je .echo_help
    
    call print_string
    mov si, MSG_NEWLINE
    call print_string
    ret
    
.echo_help:
    mov si, MSG_ECHO_HELP
    call print_string
    ret

do_pause:
    ; Ожидание нажатия клавиши
    mov si, MSG_PRESS_ANY_KEY
    call print_string
    call wait_for_key
    ret

do_type:
    ; Вывод ASCII таблицы
    mov si, .ascii_header
    call print_string
    
    xor cx, cx
    mov cl, 0
    
.ascii_loop:
    mov al, cl
    xor ah, ah
    call print_decimal
    
    mov al, ':'
    call print_char
    mov al, ' '
    call print_char
    
    mov al, cl
    call print_char
    
    mov al, ' '
    call print_char
    call print_char
    
    ; 16 символов в строке
    inc cl
    test cl, 0x0F
    jnz .same_line
    
    mov si, MSG_NEWLINE
    call print_string
    jmp .next_char
    
.same_line:
    cmp cl, 128
    jae .next_char
    
    ; Пропускаем управляющие символы
    mov al, cl
    cmp al, 0x0D
    je .skip_print
    cmp al, 0x0A
    je .skip_print
    cmp al, 0x08
    je .skip_print
    cmp al, 0x07
    je .skip_print
    
    jmp .next_char
    
.skip_print:
    mov al, ' '
    call print_char
    
.next_char:
    cmp cl, 128
    jb .ascii_loop
    
    mov si, MSG_NEWLINE
    call print_string
    ret
    
.ascii_header db 'ASCII Table (0-127):', 0x0D, 0x0A, 0

do_mouse:
    ; Инициализация мыши
    pusha
    
    ; Сброс мыши
    mov ax, 0x0000
    int 0x33
    cmp ax, 0xFFFF
    jne .no_mouse
    
    ; Мышь найдена
    ; Получаем статус
    mov ax, 0x0003
    int 0x33
    
    ; CX = X, DX = Y, BX = кнопки
    mov [mouse_x], cx
    mov [mouse_y], dx
    mov [mouse_buttons], bl
    
    mov si, MSG_MOUSE_FOUND
    call print_string
    
    mov ax, [mouse_x]
    call print_decimal
    
    mov si, MSG_MOUSE_Y
    call print_string
    
    mov ax, [mouse_y]
    call print_decimal
    
    mov si, MSG_MOUSE_BUTTONS
    call print_string
    
    mov al, [mouse_buttons]
    xor ah, ah
    call print_decimal
    
    mov si, MSG_NEWLINE
    call print_string
    popa
    ret
    
.no_mouse:
    mov si, MSG_MOUSE_NOT_FOUND
    call print_string
    popa
    ret

do_sound:
    ; Воспроизведение звука
    mov si, cmd_args
    cmp byte [si], 0
    je .sound_beep
    
    ; Пытаемся разобрать частоту
    xor ax, ax
    xor bx, bx
    
.read_freq:
    lodsb
    test al, al
    jz .play_sound
    
    sub al, '0'
    cmp al, 9
    ja .sound_error
    
    mov bl, al
    mov al, bh
    mov cl, 10
    mul cl
    add al, bl
    mov bh, al
    jmp .read_freq
    
.play_sound:
    ; bh = частота (десятки Гц)
    mov al, 0xB6
    out 0x43, al
    
    ; Преобразуем частоту в делитель
    mov ax, 1193180
    xor dx, dx
    mov bl, bh
    xor bh, bh
    div bx
    
    out 0x42, al
    mov al, ah
    out 0x42, al
    
    in al, 0x61
    or al, 0x03
    out 0x61, al
    
    ; Задержка
    mov cx, 0x0FFF
.sound_delay:
    loop .sound_delay
    
    ; Выключаем звук
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    
    mov si, .sound_done
    call print_string
    ret
    
.sound_beep:
    ; Простой бип
    call do_beep
    ret
    
.sound_error:
    mov si, .sound_error_msg
    call print_string
    ret
    
.sound_done db 'Sound played', 0x0D, 0x0A, 0
.sound_error_msg db 'Invalid frequency', 0x0D, 0x0A, 0

do_ver:
    ; Вывод версии
    mov si, MSG_VERSION
    call print_string
    ret

do_test:
    ; Системные тесты
    mov si, .test_start
    call print_string
    
    ; Тест памяти
    mov si, .test_mem
    call print_string
    call do_memtest
    
    ; Тест графики
    mov si, .test_graphics
    call print_string
    
    cmp byte [video_mode], 0x03
    jne .skip_graphics_test
    
    call init_graphics
    mov al, 15
    mov cx, 10
    mov dx, 10
    mov si, 300
    mov di, 180
    call draw_line
    call wait_for_key
    call set_text_mode
    
.skip_graphics_test:
    ; Тест звука
    mov si, .test_sound
    call print_string
    call do_beep
    
    mov si, .test_done
    call print_string
    ret
    
.test_start db 'Running system tests...', 0x0D, 0x0A, 0
.test_mem db 'Memory test: ', 0
.test_graphics db 'Graphics test... ', 0x0D, 0x0A, 0
.test_sound db 'Sound test... ', 0x0D, 0x0A, 0
.test_done db 'All tests completed.', 0x0D, 0x0A, 0

do_delay:
    ; Задержка выполнения
    mov si, cmd_args
    cmp byte [si], 0
    je .delay_default
    
    ; Читаем количество секунд
    xor ax, ax
.read_delay:
    lodsb
    test al, al
    jz .start_delay
    
    sub al, '0'
    cmp al, 9
    ja .delay_error
    
    mov bl, al
    mov al, ah
    mov cl, 10
    mul cl
    add al, bl
    mov ah, al
    jmp .read_delay
    
.start_delay:
    ; ah = секунды
    xor ch, ch
    mov cl, ah
    
.delay_loop:
    push cx
    mov cx, 0xFFFF
.inner_loop:
    push cx
    mov cx, 0x00FF
.wait_loop:
    loop .wait_loop
    pop cx
    loop .inner_loop
    pop cx
    loop .delay_loop
    
    ret
    
.delay_default:
    ; Задержка по умолчанию (1 секунда)
    mov ah, 1
    jmp .start_delay
    
.delay_error:
    mov si, .delay_error_msg
    call print_string
    ret
    
.delay_error_msg db 'Invalid delay time', 0x0D, 0x0A, 0

do_fill:
    ; fill x y width height color
    ; Пример: fill 10 10 50 30 4
    
    cmp byte [video_mode], 0x13
    jne .not_graphics_mode
    
    ; Разбираем аргументы
    mov si, cmd_args
    call .parse_number  ; x
    mov [line_x1], ax
    
    call .parse_number  ; y
    mov [line_y1], ax
    
    call .parse_number  ; width
    mov [line_x2], ax
    
    call .parse_number  ; height
    mov [line_y2], ax
    
    call .parse_number  ; color
    mov [temp_var], ax
    
    ; Рисуем прямоугольник
    mov cx, [line_x1]
    mov dx, [line_y1]
    mov si, [line_x2]
    mov di, [line_y2]
    mov al, [temp_var]
    call fill_rect
    
    mov si, .fill_done
    call print_string
    ret
    
.not_graphics_mode:
    mov si, .not_graphics_msg
    call print_string
    ret
    
.parse_number:
    xor ax, ax
    xor bx, bx
    
.skip_spaces:
    lodsb
    test al, al
    jz .parse_done
    cmp al, ' '
    je .skip_spaces
    
.parse_digit:
    sub al, '0'
    cmp al, 9
    ja .parse_done
    
    mov bl, al
    mov al, bh
    mov cl, 10
    mul cl
    add al, bl
    mov bh, al
    
    mov al, [si]
    test al, al
    jz .parse_done
    cmp al, ' '
    je .parse_done
    
    inc si
    jmp .parse_digit
    
.parse_done:
    mov al, bh
    xor ah, ah
    ret
    
.fill_done db 'Rectangle drawn', 0x0D, 0x0A, 0
.not_graphics_msg db 'Switch to graphics mode first (vga 1)', 0x0D, 0x0A, 0

do_line:
    ; line x1 y1 x2 y2 color
    cmp byte [video_mode], 0x13
    jne do_fill.not_graphics_mode
    
    mov si, cmd_args
    call do_fill.parse_number  ; x1
    mov [line_x1], ax
    
    call do_fill.parse_number  ; y1
    mov [line_y1], ax
    
    call do_fill.parse_number  ; x2
    mov [line_x2], ax
    
    call do_fill.parse_number  ; y2
    mov [line_y2], ax
    
    call do_fill.parse_number  ; color
    mov [temp_var], ax
    
    mov cx, [line_x1]
    mov dx, [line_y1]
    mov si, [line_x2]
    mov di, [line_y2]
    mov al, [temp_var]
    call draw_line
    
    mov si, .line_done
    call print_string
    ret
    
.line_done db 'Line drawn', 0x0D, 0x0A, 0

do_circle:
    ; circle x y radius color
    cmp byte [video_mode], 0x13
    jne do_fill.not_graphics_mode
    
    mov si, cmd_args
    call do_fill.parse_number  ; x
    mov [circle_x], ax
    
    call do_fill.parse_number  ; y
    mov [circle_y], ax
    
    call do_fill.parse_number  ; radius
    mov [circle_radius], ax
    
    call do_fill.parse_number  ; color
    mov [temp_var], ax
    
    mov cx, [circle_x]
    mov dx, [circle_y]
    mov si, [circle_radius]
    mov al, [temp_var]
    call draw_circle
    
    mov si, .circle_done
    call print_string
    ret
    
.circle_done db 'Circle drawn', 0x0D, 0x0A, 0

; ---------- Сообщения ----------
memtest_msg1 db 'Testing memory... ', 0
memtest_msg2 db 'Available: ', 0
memtest_msg3 db ' KB', 0x0D, 0x0A, 0
memtest_error db 'Memory test failed!', 0x0D, 0x0A, 0

cpu_info db 'CPU: i386 16-bit compatible', 0x0D, 0x0A, 0
video_info db 'Video: VGA 320x200 (256 colors) / Text 80x25', 0x0D, 0x0A, 0
memory_info db 'Memory: 640 KB conventional', 0x0D, 0x0A, 0

syscpd_msg db 'System Configuration:', 0x0D, 0x0A
           db 'Kernel: YodaOS 2.1 Enhanced', 0x0D, 0x0A
           db 'Architecture: i386 16-bit', 0x0D, 0x0A
           db 'Bootloader: Custom MBR', 0x0D, 0x0A
           db 'Graphics: VGA 13h with shapes', 0x0D, 0x0A
           db 'Commands: 30+ available', 0x0D, 0x0A
           db 'Year: 2025', 0x0D, 0x0A, 0

help_text:
    db 'Available commands:', 0x0D, 0x0A
    db '  help             - Show this help', 0x0D, 0x0A
    db '  clear            - Clear screen', 0x0D, 0x0A
    db '  memtest          - Test RAM memory', 0x0D, 0x0A
    db '  sysinfo -c/v/a   - System information', 0x0D, 0x0A
    db '  syscpd           - System configuration', 0x0D, 0x0A
    db '  shutdown -t/r    - Power off computer', 0x0D, 0x0A
    db '  reboot           - Restart computer', 0x0D, 0x0A
    db '  chsc -c/b/k/a    - System components', 0x0D, 0x0A
    db '  color <0-15>     - Change text color', 0x0D, 0x0A
    db '  saur -c/e/x/r    - User privileges', 0x0D, 0x0A
    db '  rp -change <old> <new> - Change root password', 0x0D, 0x0A
    db '  rs -cursor <text>      - Change cursor', 0x0D, 0x0A
    db '  rs -asciiart <1-3>     - Change ASCII art', 0x0D, 0x0A
    db '  date             - Show current date', 0x0D, 0x0A
    db '  time             - Show current time', 0x0D, 0x0A
    db '  beep             - Sound beep', 0x0D, 0x0A
    db '  vga 1            - Switch to graphics mode', 0x0D, 0x0A
    db '  vga 3            - Switch to text mode', 0x0D, 0x0A
    db '  graphics         - Graphics demo', 0x0D, 0x0A
    db '  text             - Return to text mode', 0x0D, 0x0A
    db '  calc <expr>      - Calculator', 0x0D, 0x0A
    db '  edit             - Line editor', 0x0D, 0x0A
    db '  draw             - Advanced graphics demo', 0x0D, 0x0A
    db '  rand             - Random number', 0x0D, 0x0A
    db '  echo <text>      - Echo text', 0x0D, 0x0A
    db '  pause            - Wait for key', 0x0D, 0x0A
    db '  type             - Show ASCII table', 0x0D, 0x0A
    db '  mouse            - Mouse test', 0x0D, 0x0A
    db '  sound [freq]     - Play sound', 0x0D, 0x0A
    db '  ver              - Version info', 0x0D, 0x0A
    db '  test             - System test', 0x0D, 0x0A
    db '  delay [sec]      - Delay execution', 0x0D, 0x0A
    db '  fill x y w h c   - Draw rectangle', 0x0D, 0x0A
    db '  line x1 y1 x2 y2 c - Draw line', 0x0D, 0x0A
    db '  circle x y r c   - Draw circle', 0x0D, 0x0A, 0

shutdown_msg db 'Shutting down system...', 0x0D, 0x0A, 0
shutdown_timed db 'System will shutdown in 3 seconds...', 0x0D, 0x0A, 0
reboot_msg db 'Rebooting...', 0x0D, 0x0A, 0

chsc_all db 'All system components:', 0x0D, 0x0A
         db '  boot.asm - Bootloader (512 bytes)', 0x0D, 0x0A
         db '  kernel.asm - Main kernel (24KB)', 0x0D, 0x0A
         db '  Graphics module - VGA 13h with shapes', 0x0D, 0x0A
         db '  New commands module - 15+ commands', 0x0D, 0x0A
         db 'Status: OK', 0x0D, 0x0A, 0
chsc_components db 'System components: OK', 0x0D, 0x0A, 0
chsc_boot db 'Bootloader: YodaOS Boot v2.1', 0x0D, 0x0A, 0
chsc_kernel db 'Kernel: YodaOS Kernel v2.1 Enhanced', 0x0D, 0x0A, 0

color_changed db 'Text color changed', 0x0D, 0x0A, 0
current_color_msg db 'Current color: ', 0
color_error db 'Invalid color (0-15)', 0x0D, 0x0A, 0

saur_user db 'Current user: standard', 0x0D, 0x0A, 0
saur_root db 'Current user: root', 0x0D, 0x0A, 0
password_prompt db 'Enter password: ', 0
password_correct_msg db 'Password correct', 0x0D, 0x0A, 0
password_wrong db 'Wrong password!', 0x0D, 0x0A, 0
exit_root_msg db 'Exited root mode', 0x0D, 0x0A, 0
enter_root_msg db 'Entered root mode', 0x0D, 0x0A, 0
already_root_msg db 'Already in root mode', 0x0D, 0x0A, 0
saur_error db 'Usage: saur -c/-e/-x/-r', 0x0D, 0x0A, 0

rp_usage db 'Usage: rp -change <old_password> <new_password>', 0x0D, 0x0A, 0
password_changed db 'Root password changed', 0x0D, 0x0A, 0

rs_usage db 'Usage: rs -cursor <text> OR rs -asciiart <1-3>', 0x0D, 0x0A, 0
cursor_changed db 'Cursor changed', 0x0D, 0x0A, 0
asciiart_changed db 'ASCII art changed', 0x0D, 0x0A, 0
asciiart_error db 'Invalid ASCII art number (1-3)', 0x0D, 0x0A, 0

date_error db 'Failed to get date', 0x0D, 0x0A, 0
time_error db 'Failed to get time', 0x0D, 0x0A, 0

current_mode db 'Current video mode: ', 0
mode13_set db 'Graphics mode 13h (320x200) activated. Press any key...', 0x0D, 0x0A, 0
mode03_set db 'Text mode 03h (80x25) activated', 0x0D, 0x0A, 0
vga_error db 'Usage: vga <1|3>  (1=graphics, 3=text)', 0x0D, 0x0A, 0
text_mode_set db 'Text mode activated', 0x0D, 0x0A, 0

beep_done db 'Beep sounded', 0x0D, 0x0A, 0

graphics_msg db 'YodaOS Graphics Demo - Press any key', 0

; ---------- Заполнение ----------
times 24576-($-$$) db 0  ; Заполняем до 24KB