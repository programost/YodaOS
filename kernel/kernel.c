#include "kernel.h"
#include "drivers.h"
#include "fs.h"
#include "ramfs.h"
#include "shell.h"
#include "string.h"
#include "log.h"

uint32_t multiboot_magic = 0;
multiboot_info_t *multiboot_info = NULL;

extern uint32_t partition_offset;

#define MULTIBOOT2_MAGIC 0x36d76289
static uint32_t mb2_mem_lower_kb;
static uint32_t mb2_mem_upper_kb;

const char* OFF = "[OFF]";
const char* ON = "[ON]";
const char* DEBUG = "[DEBUG]";
const char* INFO = "[INFO]";
const char* WARNING = "[WARNING]";
const char* ERR = "[ERROR]";

static void parse_multiboot2(uint32_t phys) {
    mb2_mem_lower_kb = 0;
    mb2_mem_upper_kb = 0;
    const uint8_t *p = (const uint8_t *)(uintptr_t)phys;
    uint32_t total = *(const uint32_t *)p;
    uint32_t off = 8;
    while (off + 8 < total && off < 32768) {
        const uint32_t *tag = (const uint32_t *)(p + off);
        uint32_t type = tag[0];
        uint32_t size = tag[1];
        if (size < 8) break;
        if (type == 0) break;
        if (type == 4 && size >= 16) {
            mb2_mem_lower_kb = tag[2];
            mb2_mem_upper_kb = tag[3];
        }
        off += (size + 7u) & ~7u;
    }
}
static uint8_t user_ring3_stack[4096] __attribute__((aligned(16)));
extern void syscall_setup(void);
/*/void syscall_init() {
    uint64_t handler = (uint64_t)syscall_entry;
    __asm__ volatile("wrmsr" : : "c"(0xC0000082), "a"((uint32_t)handler), "d"((uint32_t)(handler >> 32)));
    // Устанавливаем маску для rflags (бит 9 для IF)
    uint64_t mask = 0x200; // Бит 9 (IF) — прерывания разрешены?
    __asm__ volatile("wrmsr" : : "c"(0xC0000084), "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)));
}/*/

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

void kernel_panic_stack_test(void) {
    while (1)
        __asm__ volatile("pause" ::: "memory");
}

void panic_handler(int vector) {
    vga_clear(VGA_COLOR_BLACK | (VGA_COLOR_BLACK << 4));
    int x = VGA_WIDTH;
    int y = VGA_HEIGHT;
    const char* msg = "KERNEL PANIC\n";
    vga_set_cursor((x/2)-strlen(msg), 0);
    vga_write(msg, VGA_COLOR_LIGHT_RED);
    vga_write("Exception: ", VGA_COLOR_LIGHT_RED);
    char buf[8];
    int_to_str(vector, buf);
    vga_write(buf, VGA_COLOR_LIGHT_RED);
    if (fs_is_mounted() && fs_exists("/shlog.log")) {
        vga_write("--- System log (/shlog.log) ---\n", VGA_COLOR_LIGHT_CYAN);
        uint8_t log_buf[4096];
        uint32_t sz;
        if (fs_open("/shlog.log", log_buf, sizeof(log_buf)-1, &sz) == 0) {
            log_buf[sz] = '\0';
            vga_write((char*)log_buf, VGA_COLOR_LIGHT_GREY);
            vga_write("\n--- End of log ---\n", VGA_COLOR_LIGHT_CYAN);
        } else {
            vga_write("Failed to read log file.\n", VGA_COLOR_LIGHT_RED);
        }
    } else {
        vga_write("No log file available.\n", VGA_COLOR_LIGHT_BROWN);
    }
    vga_write("\nSystem halted. Please reboot pc!\n", VGA_COLOR_LIGHT_RED);
    for(;;) __asm__ volatile("hlt");
}

void cmd_reboot(void) {
    fs_sync_to_disk();
    ata_flush();
    vga_write("Rebooting...\n", VGA_COLOR_LIGHT_GREEN);
    reboot();
}

void cmd_shutdown(void) {
    fs_sync_to_disk();
    ata_flush();
    vga_write("Shutting down...\n", VGA_COLOR_LIGHT_GREEN);
    shutdown();
}

void cmd_sysinf(void) {
    vga_write("YodaOS 2.0 (x86_64, ELF64)\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("Kernel: YodaOS kernel 2.0-path-1.0.0\n", VGA_COLOR_LIGHT_GREY);

    if (multiboot_magic == MULTIBOOT2_MAGIC && (mb2_mem_lower_kb || mb2_mem_upper_kb)) {
        uint32_t total_kb = mb2_mem_lower_kb + mb2_mem_upper_kb;
        uint32_t total_mb = total_kb / 1024;
        char buf[32];
        int_to_str((int)total_mb, buf);
        vga_write("RAM: ", VGA_COLOR_LIGHT_GREY);
        vga_write(buf, VGA_COLOR_WHITE);
        vga_write(" MB\n", VGA_COLOR_LIGHT_GREY);
    } else if (multiboot_magic == 0x2BADB002 && multiboot_info && (multiboot_info->flags & 1)) {
        uint32_t total_kb = multiboot_info->mem_lower + multiboot_info->mem_upper;
        uint32_t total_mb = total_kb / 1024;
        char buf[32];
        int_to_str((int)total_mb, buf);
        vga_write("RAM: ", VGA_COLOR_LIGHT_GREY);
        vga_write(buf, VGA_COLOR_WHITE);
        vga_write(" MB\n", VGA_COLOR_LIGHT_GREY);
    } else {
        vga_write("RAM: unknown\n", VGA_COLOR_LIGHT_GREY);
    }

    if (disk_total_sectors > 0) {
        uint32_t disk_mb = (disk_total_sectors * 512) / (1024 * 1024);
        char buf[32];
        int_to_str(disk_mb, buf);
        vga_write("Disk: ", VGA_COLOR_LIGHT_GREY);
        vga_write(buf, VGA_COLOR_WHITE);
        vga_write(" MB total\n", VGA_COLOR_LIGHT_GREY);

        if (partition_offset > 0) {
            uint32_t part_sectors = disk_total_sectors - partition_offset;
            uint32_t part_mb = (part_sectors * 512) / (1024 * 1024);
            int_to_str(part_mb, buf);
            vga_write("Partition: ", VGA_COLOR_LIGHT_GREY);
            vga_write(buf, VGA_COLOR_WHITE);
            vga_write(" MB available\n", VGA_COLOR_LIGHT_GREY);
        }
    } else {
        vga_write("Disk: unknown\n", VGA_COLOR_LIGHT_GREY);
    }

    vga_write("Drivers: VGA, Keyboard, ATA, PC Speaker, CMOS/RTC\n", VGA_COLOR_LIGHT_GREY);
}

static const char *commands[] = {
    "reboot", "shutdown", "sysinf", "clear", "asciiart", "cpuid", "memtest",
    "rand", "date -d/-t", "pause", "format", "ring3test", "panic_test",
    "ls", "cd", "pwd", "mkdir", "rmdir", "rm -f/-d", "cat", "touch", "cp", "mv",
    "sync", "uname", "wc", "head", "tail", "grep",
    "ynan", "exec", "shlog", "mtdisk"
};
#define NUM_COMMANDS (sizeof(commands)/sizeof(commands[0]))
#define CMDS_PER_PAGE 10

void cmd_help_p(int page) {
    if (page < 1) page = 1;
    int total_pages = (NUM_COMMANDS + CMDS_PER_PAGE - 1) / CMDS_PER_PAGE;
    if (page > total_pages) page = total_pages;
    int start = (page - 1) * CMDS_PER_PAGE;
    unsigned int end = start + CMDS_PER_PAGE;
    if (end > NUM_COMMANDS) end = NUM_COMMANDS;
    vga_write("--- Help page ", VGA_COLOR_LIGHT_CYAN);
    char pnum[8];
    int_to_str(page, pnum);
    vga_write(pnum, VGA_COLOR_LIGHT_CYAN);
    vga_write(" of ", VGA_COLOR_LIGHT_CYAN);
    char tpages[8];
    int_to_str(total_pages, tpages);
    vga_write(tpages, VGA_COLOR_LIGHT_CYAN);
    vga_write(" ---\n", VGA_COLOR_LIGHT_CYAN);
    for (int i = start; i < (int)end; i++) {
        vga_write("  ", VGA_COLOR_LIGHT_GREY);
        vga_write(commands[i], VGA_COLOR_LIGHT_GREEN);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
    vga_write("Use 'help -p <page>' for more.\n", VGA_COLOR_LIGHT_GREY);
}
void cmd_clear(void) {
    vga_clear(VGA_COLOR_BLACK | (VGA_COLOR_BLACK << 4));
    vga_set_cursor(0, 0);
    vga_taskbar_refresh();
}

void cmd_asciiart(void) {
    vga_write("       $&$   $&$  /$&&&&&$\\  $$$$$$$$$\\     /$&&&$\\\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("        $&$ $&$   $&$   $&$  $&$    $&$    /$&$ $&$\\\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("         $&&&$    $&$   $&$  $&$    $&$    $&$   $&$\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("          $&$     $&$   $&$  $&$    $&$    $&&&&&&&$\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("          $&$     $&$   $&$  $&$    $&$    $&$   $&$\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("          $$$     \\$&&&&&$/  $&&&&&&&$/    $$$   $$$\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("            YodaOS - May the code be with you\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void cmd_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;
    // Vendor (leaf 0)
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
    char vendor[13] = {0};
    *(uint32_t*)(vendor) = ebx;
    *(uint32_t*)(vendor+4) = edx;
    *(uint32_t*)(vendor+8) = ecx;
    vga_write("Vendor: ", VGA_COLOR_LIGHT_GREY);
    vga_write(vendor, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    // Features (leaf 1)
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    vga_write("Model: ", VGA_COLOR_LIGHT_GREY);
    char buf[16];
    int_to_str((eax >> 4) & 0x0F, buf);
    vga_write(buf, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    vga_write("Stepping: ", VGA_COLOR_LIGHT_GREY);
    int_to_str(eax & 0x0F, buf);
    vga_write(buf, VGA_COLOR_LIGHT_CYAN);
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

void cmd_pause(void) {
    uint8_t old_x, old_y;
    vga_get_cursor(&old_x, &old_y);
    const char *msg = "Press any key to continue...";
    int len = strlen(msg);
    int x = (VGA_WIDTH - len) / 2;
    int y = VGA_HEIGHT / 2;
    uint16_t saved_line[VGA_WIDTH];
    for (int i = 0; i < VGA_WIDTH; i++)
        saved_line[i] = VGA_MEMORY[y * VGA_WIDTH + i];
    vga_set_cursor(x, y);
    vga_write(msg, VGA_COLOR_LIGHT_CYAN);
    // Ожидание нажатия любой клавиши (игнорируем старое состояние)
    uint8_t sc;
    do {
        sc = inb(0x60);
        // Ждём, пока в порте 0x64 не появится бит 1 (данные готовы)
        while (!(inb(0x64) & 1)) { __asm__ volatile("pause"); }
        sc = inb(0x60);
    } while (sc == 0); // 0 означает, что это не символ (может быть скан-код)
    // Восстановление
    for (int i = 0; i < VGA_WIDTH; i++)
        VGA_MEMORY[y * VGA_WIDTH + i] = saved_line[i];
    vga_set_cursor(old_x, old_y);
    vga_taskbar_refresh();
}

void cmd_format(void) {
    if (disk_total_sectors == 0) {
        vga_write("Error: disk size unknown.\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t part_sectors = disk_total_sectors - 1;
    vga_write("Creating MBR + Linux ext2 partition (type 0x83)...\n", VGA_COLOR_LIGHT_GREEN);
    char buf[32];
    int_to_str((int)part_sectors, buf);
    vga_write(buf, VGA_COLOR_LIGHT_GREEN);
    vga_write(" sectors.\n", VGA_COLOR_LIGHT_GREEN);

    if (disk_create_ext2_partition(1, part_sectors) != 0) {
        vga_write("Failed to write MBR.\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    ata_flush();
    partition_offset = 1;
    fs_init();
    if (fs_format_partition(part_sectors) == 0) {
        vga_write("EXT2 filesystem created and mounted.\n", VGA_COLOR_LIGHT_GREEN);
        fs_sync_to_disk();
        ata_flush();
    } else {
        vga_write("EXT2 mkfs failed.\n", VGA_COLOR_LIGHT_RED);
        partition_offset = 0;
        fs_init();
    }
}

void cmd_mkfs(void) {
    if (disk_total_sectors == 0) {
        vga_write("Error: disk size unknown.\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    vga_write("Formatting whole disk (no MBR) as ext2...\n", VGA_COLOR_LIGHT_CYAN);
    ata_flush();
    partition_offset = 0;
    fs_init();
    if (fs_format_partition(disk_total_sectors) == 0) {
        vga_write("EXT2 filesystem created and mounted.\n", VGA_COLOR_LIGHT_GREEN);
        fs_sync_to_disk();
        ata_flush();
    } else {
        vga_write("EXT2 mkfs failed.\n", VGA_COLOR_LIGHT_RED);
        partition_offset = 0;
        fs_init();
    }
}

void kprintf(const char *str, uint8_t color) {
    vga_write(str, color);
}

void kmain(uint32_t magic, uint32_t addr) {
    multiboot_magic = magic;
    multiboot_info = (multiboot_info_t *)(uintptr_t)addr;
    if (magic == MULTIBOOT2_MAGIC)
        parse_multiboot2(addr);

    vga_init();
    idt_install();
    srand(0xC001D00Du);

    dbstring(&DEBUG, "VGA, IDT, SRAND: OK");

    if (ata_init() != 0) {
        kprintf("ATA init failed\n", VGA_COLOR_LIGHT_RED);
        dbstring(&ERR, "ATA init failed.");
        while(1);
    }
    kprintf("[init] ATA: OK, ", VGA_COLOR_LIGHT_GREEN);
    dbstring(&DEBUG, "ATA: OK");
    char buf[32];
    int_to_str(disk_total_sectors, buf);
    kprintf(buf, VGA_COLOR_LIGHT_GREEN);
    kprintf(" sectors\n", VGA_COLOR_LIGHT_GREEN);
    dbstring(&INFO, buf);
    dbstring(&INFO, "ATA SECTORS ^");

    fs_init();
    dbstring(&DEBUG, "EXT2: OK");

    int mounted = 0;
    partition_offset = 1;
    if (fs_load_from_disk() == 0) {
        kprintf("[ext2] mounted at offset 1 (partition)\n", VGA_COLOR_LIGHT_GREEN);
        dbstring(&DEBUG, "EXT2: mounted at offset 1 (partition)");
        mounted = 1;
    } else {
        partition_offset = 0;
        if (fs_load_from_disk() == 0) {
            kprintf("[ext2] mounted at offset 0 (whole disk)\n", VGA_COLOR_LIGHT_GREEN);
            dbstring(&DEBUG, "EXT2: mounted at offset 0 (whole disk)");
            mounted = 1;
        }
    }
    if (!mounted) {
        kprintf("[ext2] mount failed!\n", VGA_COLOR_LIGHT_RED);
        dbstring(&ERR, "EXT2: Mount failed!");
        while(1);
    }
    fs_cd("/");
    dbstring(&DEBUG, "EXT2: Going to the directory '/' ");

    sound_init();
    kprintf("[init] PC speaker: OK\n", VGA_COLOR_LIGHT_GREEN);
    dbstring(&DEBUG, "PC speaker: OK");

    /*/uint8_t test_buf[10]; Это будущий загрузчик ELF static файлов)
    uint32_t test_sz;
    if (fs_open("/test.elf", test_buf, 10, &test_sz) == 0) {
        kprintf("test.elf opened, size=", VGA_COLOR_LIGHT_GREEN);
        char tmp[16];
        int_to_str(test_sz, tmp);
        kprintf(tmp, VGA_COLOR_LIGHT_GREEN);
        kprintf("\n", VGA_COLOR_LIGHT_GREEN);
    } else {
        kprintf("test.elf not found\n", VGA_COLOR_LIGHT_RED);
    }/*/
    dbstring(&INFO, "System started!");
    dbstring(&INFO, "Login as 'root'");
    log_save_to_file();
    fs_sync_to_disk();   
    cmd_clear();
    cmd_asciiart();
    vga_write("YodaOS version 2.0 (Beta test edition)\n", VGA_COLOR_LIGHT_GREEN);
    vga_write("if you started the system for the first time, please type mtdisk for mount ext2\n", VGA_COLOR_LIGHT_GREEN);
    shell_run();
}
