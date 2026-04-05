#include "drivers.h"
#include "kernel.h"
#include <stddef.h>

// ----- VGA -----
static uint8_t vga_x = 0, vga_y = 0;
static uint8_t vga_color = VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4);

void vga_init(void) {
    vga_clear(vga_color);
    vga_set_cursor(0, 0);
}

void vga_putchar(char c, uint8_t color) {
    if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
            VGA_MEMORY[vga_y * VGA_WIDTH + vga_x] = (color << 8) | ' ';
            vga_set_cursor(vga_x, vga_y);
        }
        return;
    }
    if (c == '\n') {
        vga_x = 0;
        if (++vga_y == VGA_HEIGHT) {
            for (int i = 1; i < VGA_HEIGHT; i++)
                for (int j = 0; j < VGA_WIDTH; j++)
                    VGA_MEMORY[(i-1)*VGA_WIDTH + j] = VGA_MEMORY[i*VGA_WIDTH + j];
            for (int j = 0; j < VGA_WIDTH; j++)
                VGA_MEMORY[(VGA_HEIGHT-1)*VGA_WIDTH + j] = (color << 8) | ' ';
            vga_y = VGA_HEIGHT - 1;
        }
    } else if (c == '\r') {
        vga_x = 0;
    } else {
        VGA_MEMORY[vga_y * VGA_WIDTH + vga_x] = (color << 8) | c;
        if (++vga_x == VGA_WIDTH) {
            vga_x = 0;
            if (++vga_y == VGA_HEIGHT) {
                for (int i = 1; i < VGA_HEIGHT; i++)
                    for (int j = 0; j < VGA_WIDTH; j++)
                        VGA_MEMORY[(i-1)*VGA_WIDTH + j] = VGA_MEMORY[i*VGA_WIDTH + j];
                for (int j = 0; j < VGA_WIDTH; j++)
                    VGA_MEMORY[(VGA_HEIGHT-1)*VGA_WIDTH + j] = (color << 8) | ' ';
                vga_y = VGA_HEIGHT - 1;
            }
        }
    }
    vga_set_cursor(vga_x, vga_y);
}

void vga_write(const char *str, uint8_t color) {
    while (*str) vga_putchar(*str++, color);
}

void vga_clear(uint8_t color) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = (color << 8) | ' ';
    vga_x = vga_y = 0;
}

void vga_set_cursor(uint8_t x, uint8_t y) {
    uint16_t pos = y * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    vga_x = x; vga_y = y;
}

void vga_get_cursor(uint8_t *x, uint8_t *y) {
    *x = vga_x; *y = vga_y;
}

// ----- Keyboard with Shift and Caps Lock -----
static uint8_t shift_pressed = 0;
static uint8_t caps_lock = 0;

// Таблица для сканкодов без Shift (нижний регистр / базовые символы)
static const char scancode_normal[] = {
    0,   0, '1','2','3','4','5','6','7','8','9','0','-','=', 0, 0,
    'q','w','e','r','t','y','u','i','o','p','[',']', 0, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};

// Таблица для сканкодов с Shift (верхний регистр / символы верхнего регистра)
static const char scancode_shift[] = {
    0,   0, '!','@','#','$','%','^','&','*','(',')','_','+', 0, 0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 0, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

#define SCANCODE_LSHIFT   0x2A
#define SCANCODE_RSHIFT   0x36
#define SCANCODE_CAPSLOCK 0x3A

char scancode_to_char(uint8_t scancode) {
    // Обработка нажатия/отпускания Shift
    if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
        shift_pressed = 1;
        return 0;
    }
    if (scancode == (SCANCODE_LSHIFT | 0x80) || scancode == (SCANCODE_RSHIFT | 0x80)) {
        shift_pressed = 0;
        return 0;
    }
    // Обработка Caps Lock (только нажатие)
    if (scancode == SCANCODE_CAPSLOCK) {
        caps_lock = !caps_lock;
        return 0;
    }
    // Игнорируем отпускания остальных клавиш
    if (scancode & 0x80) return 0;

    // Определяем базовый символ
    char c = 0;
    if (scancode < sizeof(scancode_normal)) {
        c = shift_pressed ? scancode_shift[scancode] : scancode_normal[scancode];
        // Действие Caps Lock только на буквы
        if (caps_lock && (c >= 'a' && c <= 'z')) {
            c = c - 'a' + 'A';
        } else if (caps_lock && (c >= 'A' && c <= 'Z')) {
            c = c - 'A' + 'a';
        }
    }
    // Специальные клавиши, не вошедшие в таблицы
    if (c == 0) {
        switch (scancode) {
            case 0x1C: return '\n';   // Enter
            case 0x0E: return '\b';   // Backspace
            case 0x01: return 0x1B;   // Escape
            default: return 0;
        }
    }
    return c;
}

uint8_t keyboard_get_shift(void) {
    return shift_pressed;
}

uint8_t keyboard_get_capslock(void) {
    return caps_lock;
}

// ----- ATA PIO (первичный мастер) -----
static void ata_wait_400ns(void) {
    for (int i = 0; i < 4; i++)
        inb(ATA_PRIMARY_IO + 7);
}

static int ata_poll_ready(void) {
    for (int i = 0; i < 10000; i++) {
        uint8_t status = inb(ATA_PRIMARY_IO + 7);
        if (!(status & 0x80)) {
            if (status & 0x08) return 1;
            if (status & 0x01) return 0;
        }
        ata_wait_400ns();
    }
    return 0;
}

int ata_init(void) {
    outb(ATA_PRIMARY_CTRL, 0x04);
    for (int i = 0; i < 1000; i++) inb(ATA_PRIMARY_IO + 7);
    outb(ATA_PRIMARY_CTRL, 0x00);
    
    outb(ATA_PRIMARY_IO + 6, 0xA0);
    outb(ATA_PRIMARY_IO + 2, 0x00);
    outb(ATA_PRIMARY_IO + 3, 0x00);
    outb(ATA_PRIMARY_IO + 4, 0x00);
    outb(ATA_PRIMARY_IO + 5, 0x00);
    outb(ATA_PRIMARY_IO + 7, 0xEC);
    
    if (!ata_poll_ready()) return -1;
    
    uint16_t identify[256];
    for (int i = 0; i < 256; i++)
        identify[i] = inw(ATA_PRIMARY_IO);
    
    if (identify[0] == 0 || identify[0] == 0xFFFF) return -1;
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    outb(ATA_PRIMARY_IO + 6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + 2, count);
    outb(ATA_PRIMARY_IO + 3, (uint8_t)lba);
    outb(ATA_PRIMARY_IO + 4, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + 7, 0x20);
    
    uint16_t *buf16 = (uint16_t*)buffer;
    for (int s = 0; s < count; s++) {
        if (!ata_poll_ready()) return -1;
        for (int i = 0; i < 256; i++)
            buf16[s*256 + i] = inw(ATA_PRIMARY_IO);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    outb(ATA_PRIMARY_IO + 6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + 2, count);
    outb(ATA_PRIMARY_IO + 3, (uint8_t)lba);
    outb(ATA_PRIMARY_IO + 4, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + 7, 0x30);
    
    const uint16_t *buf16 = (const uint16_t*)buffer;
    for (int s = 0; s < count; s++) {
        if (!ata_poll_ready()) return -1;
        for (int i = 0; i < 256; i++)
            outw(ATA_PRIMARY_IO, buf16[s*256 + i]);
    }
    return 0;
}

// ----- Sound (PC Speaker) -----
void sound_init(void) {
    outb(0x61, inb(0x61) & 0xFC);
}

void sound_beep(uint32_t frequency, uint32_t duration_ms) {
    uint32_t div = 1193180 / frequency;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)div);
    outb(0x42, (uint8_t)(div >> 8));
    outb(0x61, inb(0x61) | 0x03);
    for (volatile uint32_t i = 0; i < duration_ms * 1000; i++);
    outb(0x61, inb(0x61) & 0xFC);
}

void sound_off(void) {
    outb(0x61, inb(0x61) & 0xFC);
}
// ----- CMOS / RTC -----
uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void get_rtc_time(int *hour, int *minute, int *second) {
    *hour = cmos_read(0x04);
    *minute = cmos_read(0x02);
    *second = cmos_read(0x00);
    *hour = ((*hour >> 4) * 10) + (*hour & 0x0F);
    *minute = ((*minute >> 4) * 10) + (*minute & 0x0F);
    *second = ((*second >> 4) * 10) + (*second & 0x0F);
}

void get_rtc_date(int *year, int *month, int *day) {
    *year = cmos_read(0x09);
    *month = cmos_read(0x08);
    *day = cmos_read(0x07);
    *year = ((*year >> 4) * 10) + (*year & 0x0F);
    *month = ((*month >> 4) * 10) + (*month & 0x0F);
    *day = ((*day >> 4) * 10) + (*day & 0x0F);
    *year += 2000;
}