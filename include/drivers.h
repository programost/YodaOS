#ifndef DRIVERS_H
#define DRIVERS_H

#include "types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_TASKBAR_ROW   (VGA_HEIGHT - 1)
#define VGA_LAST_TEXT_ROW (VGA_HEIGHT - 2)
#define VGA_MEMORY ((uint16_t*)(uintptr_t)0xB8000)
#define VGA_COLOR_LIGHT_YELLOW VGA_COLOR_LIGHT_BROWN
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

void vga_init(void);
void vga_putchar(char c, uint8_t color);
void vga_write(const char *str, uint8_t color);
void vga_clear(uint8_t color);
void vga_set_cursor(uint8_t x, uint8_t y);
void vga_get_cursor(uint8_t *x, uint8_t *y);
void vga_taskbar_refresh(void);

char scancode_to_char(uint8_t scancode);
uint8_t keyboard_get_shift(void);
uint8_t keyboard_get_capslock(void);

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECTOR_SIZE    512

extern uint32_t disk_total_sectors;

int ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer);
void ata_flush(void);

#define PARTITION_LINUX_EXT2  0x83

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sectors_count;
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint8_t  bootstrap[446];
    mbr_partition_t partitions[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

int disk_find_ext2_partition(uint32_t *out_lba_start, uint32_t *out_sectors);
int disk_create_ext2_partition(uint32_t start_lba, uint32_t sectors);

void sound_init(void);
void sound_beep(uint32_t frequency, uint32_t duration_ms);
void sound_off(void);

uint8_t cmos_read(uint8_t reg);
void get_rtc_time(int *hour, int *minute, int *second);
void get_rtc_date(int *year, int *month, int *day);

uint8_t wait_for_key(void);

#endif
