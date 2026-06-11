#include "fs.h"
#include "kernel.h"
#include "string.h"
#include "drivers.h"

#define EI_NIDENT 16

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

#define PT_LOAD 1

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

int load_elf(const char *path, uint64_t *entry, uint64_t *stack_top) {
    #define BUF_SIZE 65536
    static uint8_t buf[BUF_SIZE];
    uint32_t sz;

    vga_write("load_elf: reading ", VGA_COLOR_LIGHT_CYAN);
    vga_write(path, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_CYAN);

    // ВРЕМЕННО: используем принудительное открытие inode 12
    if (fs_open(path, buf, BUF_SIZE, &sz) != 0) {
        vga_write("load_elf: cannot open file\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    if (sz < sizeof(Elf64_Ehdr)) {
        vga_write("load_elf: file too small\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr*)buf;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        vga_write("load_elf: bad magic\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (ehdr->e_machine != 0x3E) {
        vga_write("load_elf: not x86_64\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (ehdr->e_type != 2) {
        vga_write("load_elf: not executable\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    if (ehdr->e_phoff + ehdr->e_phnum * ehdr->e_phentsize > sz) {
        vga_write("load_elf: program headers out of bounds\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    Elf64_Phdr *phdr = (Elf64_Phdr*)(buf + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t vaddr = phdr[i].p_vaddr;
            uint64_t offset = phdr[i].p_offset;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t memsz = phdr[i].p_memsz;

            if (vaddr + memsz > 0x1000000) {
                vga_write("load_elf: segment too high\n", VGA_COLOR_LIGHT_RED);
                return -1;
            }
            if (offset + filesz > sz) {
                vga_write("load_elf: segment data out of file\n", VGA_COLOR_LIGHT_RED);
                return -1;
            }
            memcpy((void*)(uintptr_t)vaddr, buf + offset, filesz);
            if (memsz > filesz)
                memset((void*)(uintptr_t)(vaddr + filesz), 0, memsz - filesz);
        }
    }

    *entry = ehdr->e_entry;
    *stack_top = 0x600000;
    vga_write("load_elf: success\n", VGA_COLOR_LIGHT_GREEN);
    return 0;
}