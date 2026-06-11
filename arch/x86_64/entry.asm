; Точка входа для syscall
global syscall_entry
extern syscall_handler

syscall_entry:
    ; 1. Сохраняем контекст пользовательской программы
    swapgs                 ; Меняем местами GS базы (для доступа к данным ядра)
    mov [rsp], rcx         ; Сохраняем пользовательский RIP (адрес возврата)
    mov [rsp + 8], r11     ; Сохраняем пользовательские RFLAGS

    ; 2. Вызываем C-обработчик
    ; Аргументы: syscall_handler(int syscall_number, ...)
    push rbp
    mov rbp, rsp
    ; Аргументы из регистров передаём дальше (rdi, rsi, rdx, r10, r8, r9)
    call syscall_handler

    ; 3. Восстанавливаем контекст и возвращаемся в пользовательский режим
    pop rbp
    mov r11, [rsp + 8]     ; Восстанавливаем RFLAGS
    mov rcx, [rsp]         ; Восстанавливаем RIP
    swapgs                 ; Меняем обратно
    sysret                 ; Возврат в Ring 3