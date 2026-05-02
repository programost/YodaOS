#include "drivers.h"
#include "types.h"

/* int 0x80: rdi=n, rsi=a1, rdx=a2 (user rax -> n in entry stub). */
uint64_t syscall_dispatch(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a3;
    switch (n) {
    case 1: {
        const char *s = (const char *)(uintptr_t)a1;
        size_t len = (size_t)a2;
        for (size_t i = 0; i < len; i++)
            vga_putchar(s[i], VGA_COLOR_LIGHT_BROWN);
        return 0;
    }
    case 2:
        return 1;
    case 3:
        return a1;
    case 4:
        return 0;
    default:
        return (uint64_t)-1;
    }
}
