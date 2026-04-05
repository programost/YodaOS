#include "kernel.h"
#include "drivers.h"
#include "fs.h"
#include <stddef.h>
#include "string.h"

static uint32_t rand_seed = 1;
void srand(uint32_t seed) { rand_seed = seed; }
uint32_t rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

char getchar(void) {
    uint8_t sc;
    char c;
    do {
        sc = wait_for_key();
        c = scancode_to_char(sc);
    } while (c == 0);
    return c;
}

// ----- Вспомогательная функция для преобразования числа в строку (для прогресс-бара и date) -----
static void int_to_str(int num, char *out) {
    if (num == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    char temp[12];
    int i = 0;
    while (num) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    int j = 0;
    while (i > 0) out[j++] = temp[--i];
    out[j] = '\0';
}

// ----- Команды -----
void cmd_reboot(void) {
    fs_sync_to_disk();
    vga_write("Rebooting...\n", VGA_COLOR_LIGHT_GREEN);
    reboot();
}

void cmd_shutdown(void) {
    fs_sync_to_disk();
    vga_write("Shutting down...\n", VGA_COLOR_LIGHT_GREEN);
    shutdown();
}

void cmd_sysinf(void) {
    vga_write("YodaOS 1.0 (i386) - 32-bit\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("Kernel: YodaOS kernel (c) 1996\n", VGA_COLOR_LIGHT_GREY);
    vga_write("Drivers: VGA, Keyboard (polling), ATA, PC Speaker, CMOS/RTC\n", VGA_COLOR_LIGHT_GREY);
}

static const char *commands[] = {
    "reboot", "shutdown", "sysinf", "help -p", "clear", "asciiart",
    "cpuid", "memtest", "rand", "date -d/-t", "pause",
    "ls", "cat", "touch", "pwd", "ynan", "rm"
};
#define NUM_COMMANDS (sizeof(commands)/sizeof(commands[0]))
#define CMDS_PER_PAGE 10

void cmd_help_p(int page) {
    if (page < 1) page = 1;
    int total_pages = (NUM_COMMANDS + CMDS_PER_PAGE - 1) / CMDS_PER_PAGE;
    if (page > total_pages) page = total_pages;
    int start = (page - 1) * CMDS_PER_PAGE;
    int end = start + CMDS_PER_PAGE;
    if (end > NUM_COMMANDS) end = NUM_COMMANDS;
    vga_write("--- Help page ", VGA_COLOR_LIGHT_CYAN);
    char pnum[4]; pnum[0] = '0' + page; pnum[1] = 0;
    vga_write(pnum, VGA_COLOR_LIGHT_CYAN);
    vga_write(" of ", VGA_COLOR_LIGHT_CYAN);
    char tpages[4]; tpages[0] = '0' + total_pages; tpages[1] = 0;
    vga_write(tpages, VGA_COLOR_LIGHT_CYAN);
    vga_write(" ---\n", VGA_COLOR_LIGHT_CYAN);
    for (int i = start; i < end; i++) {
        vga_write("  ", VGA_COLOR_LIGHT_GREY);
        vga_write(commands[i], VGA_COLOR_LIGHT_GREEN);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
    vga_write("Use 'help -p <page>' for more.\n", VGA_COLOR_LIGHT_GREY);
}

void cmd_clear(void) {
    vga_clear(VGA_COLOR_BLACK | (VGA_COLOR_BLACK << 4));
    vga_set_cursor(0, 0);
}

void cmd_asciiart(void) {
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    vga_write("  YYYY   OOO   DDD    AAA    OOO   SSS\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("  Y  Y  O   O  D  D  A   A  O   O  S\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("  Y  Y  O   O  D   D AAAAA  O   O   SS\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("  YYY   O   O  D  D  A   A  O   O     S\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("  Y     OOO   DDD   A   A  OOO   SSS\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("            YodaOS - May the code be with you\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void cmd_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    vga_write("Vendor: ", VGA_COLOR_LIGHT_GREY);
    char vendor[13] = {0};
    *(uint32_t*)(vendor) = ebx;
    *(uint32_t*)(vendor+4) = edx;
    *(uint32_t*)(vendor+8) = ecx;
    vga_write(vendor, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void cmd_memtest(void) {
    vga_write("Testing memory region at 0x400000 (1MB)...\n", VGA_COLOR_LIGHT_GREEN);
    volatile uint8_t *mem = (uint8_t*)0x400000;
    int errors = 0;
    for (uint32_t i = 0; i < 1024 * 1024; i++)
        mem[i] = (i & 0xFF);
    for (uint32_t i = 0; i < 1024 * 1024; i++)
        if (mem[i] != (i & 0xFF)) errors++;
    if (errors == 0)
        vga_write("Memory test passed.\n", VGA_COLOR_LIGHT_GREEN);
    else
        vga_write("Memory test FAILED!\n", VGA_COLOR_LIGHT_RED);
}

void cmd_rand(void) {
    uint32_t r = rand();
    vga_write("Random: 0x", VGA_COLOR_LIGHT_GREY);
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (r >> (i*4)) & 0xF;
        char c = nibble < 10 ? '0'+nibble : 'A'+nibble-10;
        vga_putchar(c, VGA_COLOR_LIGHT_CYAN);
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

// ---------- ИСПРАВЛЕННАЯ КОМАНДА DATE ----------
void cmd_date(int show_date, int show_time) {
    if (show_date) {
        int year, month, day;
        get_rtc_date(&year, &month, &day);
        vga_write("Date: ", VGA_COLOR_LIGHT_GREY);
        char num_str[6];
        int_to_str(year, num_str);
        vga_write(num_str, VGA_COLOR_LIGHT_GREY);
        vga_write("-", VGA_COLOR_LIGHT_GREY);
        int_to_str(month, num_str);
        if (month < 10) vga_write("0", VGA_COLOR_LIGHT_GREY);
        vga_write(num_str, VGA_COLOR_LIGHT_GREY);
        vga_write("-", VGA_COLOR_LIGHT_GREY);
        int_to_str(day, num_str);
        if (day < 10) vga_write("0", VGA_COLOR_LIGHT_GREY);
        vga_write(num_str, VGA_COLOR_LIGHT_GREY);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
    if (show_time) {
        int hour, minute, second;
        get_rtc_time(&hour, &minute, &second);
        vga_write("Time: ", VGA_COLOR_LIGHT_GREY);
        char num_str[3];
        int_to_str(hour, num_str);
        if (hour < 10) vga_write("0", VGA_COLOR_LIGHT_GREY);
        vga_write(num_str, VGA_COLOR_LIGHT_GREY);
        vga_write(":", VGA_COLOR_LIGHT_GREY);
        int_to_str(minute, num_str);
        if (minute < 10) vga_write("0", VGA_COLOR_LIGHT_GREY);
        vga_write(num_str, VGA_COLOR_LIGHT_GREY);
        vga_write(":", VGA_COLOR_LIGHT_GREY);
        int_to_str(second, num_str);
        if (second < 10) vga_write("0", VGA_COLOR_LIGHT_GREY);
        vga_write(num_str, VGA_COLOR_LIGHT_GREY);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
}

// ---------- ИСПРАВЛЕННАЯ КОМАНДА PAUSE ----------
void cmd_pause(void) {
    uint8_t old_x, old_y;
    vga_get_cursor(&old_x, &old_y);
    const char *msg = "Press any key to continue...";
    int len = strlen(msg);
    int x = (VGA_WIDTH - len) / 2;
    int y = VGA_HEIGHT / 2;
    // Сохраняем строку, на которой будем выводить сообщение
    uint16_t saved_line[VGA_WIDTH];
    for (int i = 0; i < VGA_WIDTH; i++)
        saved_line[i] = VGA_MEMORY[y * VGA_WIDTH + i];
    vga_set_cursor(x, y);
    vga_write(msg, VGA_COLOR_LIGHT_CYAN);
    // Ждём любую клавишу
    wait_for_key();
    // Восстанавливаем строку
    for (int i = 0; i < VGA_WIDTH; i++)
        VGA_MEMORY[y * VGA_WIDTH + i] = saved_line[i];
    // Возвращаем курсор
    vga_set_cursor(old_x, old_y);
}



static void show_progress(int current, int total, const char *label) {
    char buf[80];
    int percent = (current * 100) / total;
    int bar_len = 20;
    int filled = (percent * bar_len) / 100;
    int i;
    char *p = buf;
    *p++ = '[';
    for (i = 0; i < bar_len; i++)
        *p++ = (i < filled) ? '#' : '.';
    *p++ = ']';
    *p++ = ' ';
    char perc_str[4];
    int_to_str(percent, perc_str);
    int len = strlen(perc_str);
    if (len == 1) {
        *p++ = ' ';
        *p++ = ' ';
    } else if (len == 2) {
        *p++ = ' ';
    }
    strcpy(p, perc_str);
    p += len;
    *p++ = '%';
    *p++ = ' ';
    strcpy(p, label);
    uint8_t old_x, old_y;
    vga_get_cursor(&old_x, &old_y);
    vga_set_cursor(0, VGA_HEIGHT - 1);
    vga_write(buf, VGA_COLOR_LIGHT_CYAN);
    vga_set_cursor(old_x, old_y);
}

// ----- Оболочка (shell) -----
void shell(void) {
    char cmd[64];
    int cmd_pos = 0;
    vga_write("\n$> ", VGA_COLOR_LIGHT_GREEN);
    while (1) {
        char c = getchar();
        if (c == '\n' || c == '\r') {
            cmd[cmd_pos] = 0;
            vga_write("\n", VGA_COLOR_LIGHT_GREY);
            if (strcmp(cmd, "reboot") == 0) cmd_reboot();
            else if (strcmp(cmd, "shutdown") == 0) cmd_shutdown();
            else if (strcmp(cmd, "sysinf") == 0) cmd_sysinf();
            else if (strncmp(cmd, "help -p", 7) == 0) {
                int page = 1;
                const char *p = cmd + 7;
                while (*p == ' ') p++;
                if (*p >= '0' && *p <= '9') page = *p - '0';
                cmd_help_p(page);
            }
            else if (strncmp(cmd, "mkdir ", 6) == 0) {
                if (fs_mkdir(cmd+6) == 0)
                    vga_write("Directory created\n", VGA_COLOR_LIGHT_GREEN);
                else
                    vga_write("Failed to create directory\n", VGA_COLOR_LIGHT_RED);
            }
            else if (strncmp(cmd, "cd ", 3) == 0) {
                if (fs_cd(cmd+3) != 0)
                    vga_write("No such directory\n", VGA_COLOR_LIGHT_RED);
            }
            else if (strcmp(cmd, "cd") == 0) fs_cd("/");
            else if (strcmp(cmd, "clear") == 0) cmd_clear();
            else if (strcmp(cmd, "asciiart") == 0) cmd_asciiart();
            else if (strcmp(cmd, "cpuid") == 0) cmd_cpuid();
            else if (strcmp(cmd, "memtest") == 0) cmd_memtest();
            else if (strcmp(cmd, "rand") == 0) cmd_rand();
            else if (strncmp(cmd, "date", 4) == 0) {
                int d=0, t=0;
                if (strstr(cmd, "-d")) d=1;
                if (strstr(cmd, "-t")) t=1;
                cmd_date(d, t);
            }
            else if (strcmp(cmd, "pause") == 0) cmd_pause();
            else if (strcmp(cmd, "ls") == 0) cmd_ls();
            else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
            else if (strncmp(cmd, "cat ", 4) == 0) cmd_cat(cmd+4);
            else if (strncmp(cmd, "touch ", 6) == 0) cmd_touch(cmd+6);
            else if (strncmp(cmd, "ynan ", 5) == 0) cmd_ynan(cmd+5);
            else if (strncmp(cmd, "rm ", 3) == 0) cmd_rm(cmd+3);
            else if (cmd[0] != 0) vga_write("Unknown command\n", VGA_COLOR_LIGHT_RED);
            cmd_pos = 0;
            vga_write("$> ", VGA_COLOR_LIGHT_GREEN);
        }
        else if (c == '\b') {
            if (cmd_pos > 0) {
                cmd_pos--;
                vga_putchar('\b', VGA_COLOR_LIGHT_GREY);
            }
        }
        else if (c >= ' ' && c <= '~' && cmd_pos < 63) {
            cmd[cmd_pos++] = c;
            vga_putchar(c, VGA_COLOR_LIGHT_GREY);
        }
    }
}

// ----- Точка входа -----
void kmain(uint32_t magic, uint32_t addr) {
    vga_init();
    vga_write("YodaOS 1.0 (32-bit) for i386\n", VGA_COLOR_LIGHT_CYAN);
    if (ata_init() == 0)
        vga_write("ATA drive detected.\n", VGA_COLOR_LIGHT_GREEN);
    else
        vga_write("No ATA drive. Using RAM-only FS.\n", VGA_COLOR_LIGHT_RED);
    sound_init();
    fs_init();

    const char *init_items[] = {"DRV", "BOOT", "KRN", "USR", "TMP", "kernel_panic.sysdump.bin"};
    int total = sizeof(init_items) / sizeof(init_items[0]);
    for (int i = 0; i < total; i++) {
        show_progress(i, total, init_items[i]);
        if (i < 5)
            fs_create(init_items[i], 1);
        else
            fs_create(init_items[i], 0);
    }
    show_progress(total, total, "Done!");
    cmd_clear();
    vga_write("YodaOS 1.0 (32-bit) for i386\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("ATA drive detected.\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("Filesystem ready.\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("Type 'help -p' for commands.\n", VGA_COLOR_LIGHT_GREY);
    shell();
}