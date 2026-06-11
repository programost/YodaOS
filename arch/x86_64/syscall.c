#include "drivers.h"
#include "types.h"
#include "string.h"

// Номера системных вызовов Linux x86_64 (минимальный набор)
#define SYS_WRITE   1
#define SYS_EXIT    60
#define SYS_GETPID  39
#define SYS_BRK     12
// для демо: оставим getpid как заглушку

// extern uint64_t syscall_dispatch(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);
uint64_t syscall_dispatch(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    switch (n) {
        case SYS_WRITE: {
            int fd = (int)a1;
            const char *buf = (const char *)(uintptr_t)a2;
            size_t len = (size_t)a3;
            if (fd == 1 || fd == 2) { // stdout / stderr
                for (size_t i = 0; i < len; i++)
                    vga_putchar(buf[i], VGA_COLOR_LIGHT_GREY);
                return len;
            }
            return -1; // EBADF
        }
        case SYS_GETPID:
            return 1; // всегда PID 1
        case SYS_BRK: {
            static uint64_t current_brk = 0x600000;
            uint64_t addr = a1;
            if (addr == 0)
                return current_brk;
            if (addr < current_brk)
                return (uint64_t)-1; // не разрешаем уменьшать
            if (addr > 0x800000)
                return (uint64_t)-1; // ограничение
            current_brk = addr;
            return current_brk;
        }
        case SYS_EXIT:
            vga_write("[syscall] exit called\n", VGA_COLOR_LIGHT_BROWN);
            // В реальной системе нужно завершить процесс и вернуться в шелл
            // Пока просто бесконечный цикл
            while(1) __asm__("hlt");
        default:
            return (uint64_t)-1;
    }
}