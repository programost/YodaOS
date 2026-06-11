section .text
global isr80_asm
global isr_gpf
global isr_page_fault
global isr_of
extern syscall_dispatch
extern panic_handler
global syscall_entry
extern syscall_dispatch

syscall_entry:
    swapgs
    mov [rsp], rcx
    mov [rsp+8], r11
    ; Вызов syscall_dispatch(rax, rdi, rsi, rdx, r10, r8, r9)
    mov rdi, rax
    mov rsi, rdi
    mov rdx, rsi
    mov rcx, rdx
    mov r8, r10
    mov r9, r8
    mov r10, r9
    call syscall_dispatch
    mov r11, [rsp+8]
    mov rcx, [rsp]
    swapgs
    sysret
; ------------------------------------------------------------
; Системный вызов int 0x80 (Linux x86_64 номера)
; ------------------------------------------------------------
isr80_asm:
    ; Сохраняем контекст (все регистры общего назначения)
    push rbp
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    ; Вызов syscall_dispatch(rax, rdi, rsi, rdx, r10, r8, r9)
    mov rdi, rax          ; номер syscall
    mov rsi, rdi          ; arg1 (оригинальный rdi)
    mov rdx, rsi          ; arg2 (оригинальный rsi)
    mov rcx, rdx          ; arg3 (оригинальный rdx)
    mov r8,  r10          ; arg4
    mov r9,  r8           ; arg5
    mov r10, r9           ; arg6
    call syscall_dispatch

    ; Возвращаемое значение в rax
    mov rbx, rax
    ; Восстанавливаем регистры (кроме rbx, который используем для возврата)
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rbp
    mov rax, rbx
    iretq

; ------------------------------------------------------------
; Обработчики исключений (kernel panic)
; ------------------------------------------------------------
%macro PANIC_HANDLER 1
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, %1
    call panic_handler
    ; panic_handler не возвращается, но на всякий случай:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
%endmacro

isr_gpf:
    PANIC_HANDLER 13

isr_page_fault:
    PANIC_HANDLER 14

isr_of:
    PANIC_HANDLER 4