#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

// asm functions
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);
void cpuid(uint32_t code, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d);
void reboot(void);
void shutdown(void);
uint8_t wait_for_key(void);

// commands
void cmd_reboot(void);
void cmd_shutdown(void);
void cmd_sysinf(void);
void cmd_help_p(int page);   
void cmd_clear(void);
void cmd_asciiart(void);
void cmd_cpuid(void);
void cmd_memtest(void);
void cmd_rand(void);
void cmd_date(int show_date, int show_time);
void cmd_pause(void);

// shell
void shell(void);
char getchar(void);

#endif