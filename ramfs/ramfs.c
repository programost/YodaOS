#include "ramfs.h"
#include "drivers.h"
#include "fs.h"
#include "kernel.h"
#include "string.h"

#define BB_MAX_NODES 220
#define BB_POOL (320 * 1024)
#define BB_NAME_MAX 48
#define BB_PATH_MAX 256

#define BB_KIND_DIR 0
#define BB_KIND_FILE 1
#define BB_KIND_SPECIAL 2
#define BB_KIND_MOUNT 3

#define SP_NONE 0
#define SP_PROC_CPUINFO 1
#define SP_PROC_MEMINFO 2
#define SP_PROC_VERSION 3
#define SP_PROC_CMDLINE 4
#define SP_PROC_STAT 5
#define SP_PROC_UPTIME 6
#define SP_PROC_MOUNTS 7
#define SP_SYS_KERNEL_VER 8
#define SP_SYS_KERNEL_REL 9
#define SP_DEV_ZERO 10
#define SP_DEV_URANDOM 11
#define SP_DEV_TTY 12
#define SP_SBIN_HALT 13

static uint8_t pool[BB_POOL];
static size_t pool_used;

static struct {
    uint16_t parent;
    char name[BB_NAME_MAX];
    uint8_t kind;
    uint8_t spec;
    uint32_t data_off;
    uint32_t data_len;
    uint8_t used;
} nodes[BB_MAX_NODES];

static char bb_cwd[BB_PATH_MAX] = "/";

static int looks_disk(const char *p) {
    if (strncmp(p, "/disk", 5) != 0) return 0;
    return p[5] == 0 || p[5] == '/';
}

void bb_disk_to_ext2(const char *bb_abs, char *ext2_out, size_t olen) {
    const char *s = bb_abs + 5;
    if (*s == 0) {
        strncpy(ext2_out, "/", olen - 1);
        ext2_out[olen - 1] = 0;
        return;
    }
    strncpy(ext2_out, s, olen - 1);
    ext2_out[olen - 1] = 0;
}

int bb_path_is_disk_mounted(const char *bb_abs) {
    if (!looks_disk(bb_abs)) return 0;
    return fs_is_mounted();
}

static void bb_normalize(char *path) {
    char segs[24][48];
    int d = 0;
    const char *s = path;
    if (*s == '/') s++;
    while (*s && d < 24) {
        char comp[48];
        int k = 0;
        while (*s && *s != '/' && k < 47) comp[k++] = *s++;
        comp[k] = 0;
        while (*s == '/') s++;
        if (!comp[0]) continue;
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            if (d > 0) d--;
            continue;
        }
        strncpy(segs[d], comp, 47);
        segs[d][47] = 0;
        d++;
    }
    char *w = path;
    *w++ = '/';
    if (d == 0) {
        *w = 0;
        return;
    }
    for (int i = 0; i < d; i++) {
        if (i) *w++ = '/';
        const char *q = segs[i];
        while (*q) *w++ = *q++;
    }
    *w = 0;
}

void bb_to_abs(const char *cwd, const char *in_path, char *out_abs, size_t olen) {
    if (!in_path || !out_abs || olen < 4) return;
    if (in_path[0] == '/') {
        strncpy(out_abs, in_path, olen - 1);
        out_abs[olen - 1] = 0;
    } else {
        if (cwd[0] == '/' && cwd[1] == 0) {
            out_abs[0] = '/';
            strncpy(out_abs + 1, in_path, olen - 2);
            out_abs[olen - 1] = 0;
        } else {
            strncpy(out_abs, cwd, olen - 1);
            out_abs[olen - 1] = 0;
            size_t L = strlen(out_abs);
            if (L + 2 < olen) {
                out_abs[L] = '/';
                strncpy(out_abs + L + 1, in_path, olen - L - 2);
                out_abs[olen - 1] = 0;
            }
        }
    }
    bb_normalize(out_abs);
}

void ramfs_init(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(pool, 0, sizeof(pool));
    pool_used = 0;
    bb_cwd[0] = '/';
    bb_cwd[1] = 0;

    // Создаём корневой узел (id 0) – пустой каталог
    nodes[0].used = 1;
    nodes[0].parent = 0;
    nodes[0].name[0] = 0;      // имя пустое
    nodes[0].kind = BB_KIND_DIR;
    nodes[0].spec = SP_NONE;
    nodes[0].data_off = 0;
    nodes[0].data_len = 0;
}

size_t ramfs_bytes_used(void) { return pool_used; }
size_t ramfs_bytes_total(void) { return BB_POOL; }

int ramfs_usage_percent(void) {
    if (BB_POOL == 0) return 0;
    return (int)((pool_used * 100) / BB_POOL);
}

static int node_alloc(uint16_t parent, const char *nm, uint8_t kind, uint8_t spec) {
    for (int i = 0; i < BB_MAX_NODES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].parent = parent;
            strncpy(nodes[i].name, nm, BB_NAME_MAX - 1);
            nodes[i].name[BB_NAME_MAX - 1] = 0;
            nodes[i].kind = kind;
            nodes[i].spec = spec;
            nodes[i].data_off = 0;
            nodes[i].data_len = 0;
            return i;
        }
    }
    return -1;
}

static int node_child(int parent, const char *nm) {
    for (int i = 0; i < BB_MAX_NODES; i++) {
        if (!nodes[i].used) continue;
        if (nodes[i].parent == (uint16_t)parent && strcmp(nodes[i].name, nm) == 0) return i;
    }
    return -1;
}

static int bb_walk(const char *abs) {
    if (abs[0] != '/' || looks_disk(abs)) return -1;
    if (abs[0] == '/' && abs[1] == 0) return 0;
    int cur = 0;
    const char *p = abs + 1;
    while (*p) {
        char comp[48];
        int k = 0;
        while (*p && *p != '/' && k < 47) comp[k++] = *p++;
        comp[k] = 0;
        while (*p == '/') p++;
        if (!comp[0]) break;
        int nx = node_child(cur, comp);
        if (nx < 0) return -1;
        cur = nx;
    }
    return cur;
}

static int file_put(int parent, const char *nm, const void *data, size_t len) {
    if (pool_used + len > BB_POOL) return -1;
    int id = node_alloc(parent, nm, BB_KIND_FILE, SP_NONE);
    if (id < 0) return -1;
    memcpy(pool + pool_used, data, len);
    nodes[id].data_off = (uint32_t)pool_used;
    nodes[id].data_len = (uint32_t)len;
    pool_used += len;
    return id;
}

static int spec_put(int parent, const char *nm, uint8_t spec) {
    return node_alloc(parent, nm, BB_KIND_SPECIAL, spec);
}

static int dir_put(int parent, const char *nm) {
    return node_alloc(parent, nm, BB_KIND_DIR, SP_NONE);
}

static void special_cat(uint8_t sp) {
    uint32_t eax, ebx, ecx, edx;
    switch (sp) {
    case SP_PROC_CPUINFO:
        cpuid(0, &eax, &ebx, &ecx, &edx);
        {
            char vend[13] = {0};
            *(uint32_t *)vend = ebx;
            *(uint32_t *)(vend + 4) = edx;
            *(uint32_t *)(vend + 8) = ecx;
            vga_write("processor\t: 0\n", VGA_COLOR_LIGHT_GREY);
            vga_write("vendor_id\t: ", VGA_COLOR_LIGHT_GREY);
            vga_write(vend, VGA_COLOR_LIGHT_CYAN);
            vga_write("\n", VGA_COLOR_LIGHT_GREY);
        }
        cpuid(1, &eax, &ebx, &ecx, &edx);
        vga_write("model name\t: YodaOS virtual CPU\n", VGA_COLOR_LIGHT_GREY);
        vga_write("flags\t\t: syscall nx lm\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_PROC_MEMINFO:
        vga_write("MemTotal:       65536 kB\n", VGA_COLOR_LIGHT_GREY);
        vga_write("MemFree:        32000 kB\n", VGA_COLOR_LIGHT_GREY);
        vga_write("Buffers:            0 kB\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_PROC_VERSION:
        vga_write("Linux version 0.0-yoda (builder@yodaos) (gcc) #1\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_PROC_CMDLINE:
        vga_write("BOOT_IMAGE=/boot/kernel.elf quiet\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_PROC_STAT:
        vga_write("cpu  1 0 2 100 0 0 0 0 0 0\n", VGA_COLOR_LIGHT_GREY);
        vga_write("intr 42 0 0 0\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_PROC_UPTIME:
        vga_write("0.00 0.00\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_PROC_MOUNTS:
        vga_write("ramfs / ramfs rw 0 0\n", VGA_COLOR_LIGHT_GREY);
        vga_write("ext2 /disk ext2 rw 0 0\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_SYS_KERNEL_VER:
        vga_write("0.0-yoda\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_SYS_KERNEL_REL:
        vga_write("yodaos+unknown\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_DEV_ZERO: {
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 72; c++) vga_putchar('0', VGA_COLOR_LIGHT_GREY);
            vga_putchar('\n', VGA_COLOR_LIGHT_GREY);
        }
        break;
    }
    case SP_DEV_URANDOM:
        for (int i = 0; i < 128; i++) {
            uint32_t v = rand();
            for (int h = 7; h >= 0; h--) {
                uint8_t nib = (uint8_t)((v >> (h * 4)) & 0xF);
                vga_putchar(nib < 10 ? (char)('0' + nib) : (char)('A' + nib - 10), VGA_COLOR_LIGHT_CYAN);
            }
            if ((i & 7) == 7) vga_putchar('\n', VGA_COLOR_LIGHT_GREY);
        }
        break;
    case SP_DEV_TTY:
        vga_write("/dev/tty0: virtual console (VGA)\n", VGA_COLOR_LIGHT_GREY);
        break;
    case SP_SBIN_HALT:
        vga_write("halt: call shutdown from shell\n", VGA_COLOR_LIGHT_GREY);
        break;
    default:
        vga_write("(empty)\n", VGA_COLOR_LIGHT_GREY);
        break;
    }
}

void bb_get_cwd(char *buf, size_t len) {
    if (!buf || len == 0) return;
    strncpy(buf, bb_cwd, len - 1);
    buf[len - 1] = 0;
}

int bb_chdir(const char *path) {
    char tmp[BB_PATH_MAX];
    bb_to_abs(bb_cwd, path, tmp, sizeof(tmp));
    if (looks_disk(tmp)) {
        if (!fs_is_mounted()) {
            vga_write("cd: ext2 not mounted (use format)\n", VGA_COLOR_LIGHT_RED);
            return -1;
        }
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(tmp, e2, sizeof(e2));
        if (fs_cd(e2) != 0) {
            vga_write("cd: no such ext2 directory\n", VGA_COLOR_LIGHT_RED);
            return -1;
        }
        strncpy(bb_cwd, tmp, sizeof(bb_cwd) - 1);
        bb_cwd[sizeof(bb_cwd) - 1] = 0;
        return 0;
    }
    if (fs_is_mounted())
        (void)fs_cd("/");
    int nid = bb_walk(tmp);
    if (nid < 0) {
        vga_write("cd: not found\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (nodes[nid].kind == BB_KIND_MOUNT) {
        if (!fs_is_mounted()) {
            vga_write("cd: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
            return -1;
        }
        if (fs_cd("/") != 0) return -1;
        strncpy(bb_cwd, tmp, sizeof(bb_cwd) - 1);
        bb_cwd[sizeof(bb_cwd) - 1] = 0;
        return 0;
    }
    if (nodes[nid].kind != BB_KIND_DIR) {
        vga_write("cd: not a directory\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    strncpy(bb_cwd, tmp, sizeof(bb_cwd) - 1);
    bb_cwd[sizeof(bb_cwd) - 1] = 0;
    return 0;
}

void bb_list_cwd(void) {
    if (looks_disk(bb_cwd)) {
        if (!fs_is_mounted()) {
            vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(bb_cwd, e2, sizeof(e2));
        fs_list_at_path(e2);
        return;
    }
    int nid = bb_walk(bb_cwd);
    if (nid < 0) {
        vga_write("(bad path)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    for (int i = 0; i < BB_MAX_NODES; i++) {
        if (!nodes[i].used) continue;
        if (nodes[i].parent != (uint16_t)nid) continue;
        vga_write(nodes[i].name, VGA_COLOR_LIGHT_GREY);
        if (nodes[i].kind == BB_KIND_DIR || nodes[i].kind == BB_KIND_MOUNT)
            vga_write("/", VGA_COLOR_LIGHT_CYAN);
        else if (nodes[i].kind == BB_KIND_SPECIAL)
            vga_write("@", VGA_COLOR_LIGHT_BROWN);
        vga_write("  ", VGA_COLOR_LIGHT_GREY);
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void bb_list_abs(const char *abs_in) {
    char abs[BB_PATH_MAX];
    strncpy(abs, abs_in, sizeof(abs) - 1);
    abs[sizeof(abs) - 1] = 0;
    bb_normalize(abs);
    if (looks_disk(abs)) {
        if (!fs_is_mounted()) {
            vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        fs_list_at_path(e2);
        return;
    }
    int nid = bb_walk(abs);
    if (nid < 0) {
        vga_write("ls: not found\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if (nodes[nid].kind != BB_KIND_DIR && nodes[nid].kind != BB_KIND_MOUNT) {
        vga_write("ls: not a directory\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    for (int i = 0; i < BB_MAX_NODES; i++) {
        if (!nodes[i].used) continue;
        if (nodes[i].parent != (uint16_t)nid) continue;
        vga_write(nodes[i].name, VGA_COLOR_LIGHT_GREY);
        if (nodes[i].kind == BB_KIND_DIR || nodes[i].kind == BB_KIND_MOUNT)
            vga_write("/", VGA_COLOR_LIGHT_CYAN);
        else if (nodes[i].kind == BB_KIND_SPECIAL)
            vga_write("@", VGA_COLOR_LIGHT_BROWN);
        vga_write("  ", VGA_COLOR_LIGHT_GREY);
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void fmt_mode_ram(uint8_t kind, char *o) {
    o[0] = (kind == BB_KIND_DIR || kind == BB_KIND_MOUNT) ? 'd' : '-';
    memcpy(o + 1, "rwxr-xr-x", 9);
    o[10] = 0;
}

void bb_list_long_cwd(int show_all) {
    if (looks_disk(bb_cwd)) {
        if (!fs_is_mounted()) {
            vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(bb_cwd, e2, sizeof(e2));
        fs_list_long_at_path(e2, show_all);
        return;
    }
    int nid = bb_walk(bb_cwd);
    if (nid < 0) {
        vga_write("(bad path)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if (show_all && nid == 0) {
        vga_write("drwxr-xr-x 1 root root 0 0 .\n", VGA_COLOR_LIGHT_GREY);
        vga_write("drwxr-xr-x 1 root root 0 0 ..\n", VGA_COLOR_LIGHT_GREY);
    }
    for (int i = 0; i < BB_MAX_NODES; i++) {
        if (!nodes[i].used) continue;
        if (nodes[i].parent != (uint16_t)nid) continue;
        char md[12];
        fmt_mode_ram(nodes[i].kind, md);
        vga_write(md, VGA_COLOR_LIGHT_GREY);
        vga_write(" 1 root root ", VGA_COLOR_LIGHT_GREY);
        char szb[16];
        int_to_str((int)nodes[i].data_len, szb);
        vga_write(szb, VGA_COLOR_LIGHT_CYAN);
        vga_write(" 0 ", VGA_COLOR_LIGHT_GREY);
        vga_write(nodes[i].name, VGA_COLOR_LIGHT_GREEN);
        if (nodes[i].kind == BB_KIND_DIR || nodes[i].kind == BB_KIND_MOUNT)
            vga_write("/", VGA_COLOR_LIGHT_CYAN);
        else if (nodes[i].kind == BB_KIND_SPECIAL)
            vga_write("@", VGA_COLOR_LIGHT_BROWN);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
}

int bb_lookup_path(const char *abs_in) {
    char abs[BB_PATH_MAX];
    strncpy(abs, abs_in, sizeof(abs) - 1);
    abs[sizeof(abs) - 1] = 0;
    bb_normalize(abs);
    if (looks_disk(abs)) return -2;
    return bb_walk(abs);
}

void bb_list_long_abs(const char *abs_in, int show_all) {
    char abs[BB_PATH_MAX];
    strncpy(abs, abs_in, sizeof(abs) - 1);
    abs[sizeof(abs) - 1] = 0;
    bb_normalize(abs);
    if (looks_disk(abs)) {
        if (!fs_is_mounted()) {
            vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        fs_list_long_at_path(e2, show_all);
        return;
    }
    int nid = bb_walk(abs);
    if (nid < 0) {
        vga_write("ls: not found\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if (nodes[nid].kind != BB_KIND_DIR && nodes[nid].kind != BB_KIND_MOUNT) {
        vga_write("ls: not a directory\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if (show_all && nid == 0) {
        vga_write("drwxr-xr-x 1 root root 0 0 .\n", VGA_COLOR_LIGHT_GREY);
        vga_write("drwxr-xr-x 1 root root 0 0 ..\n", VGA_COLOR_LIGHT_GREY);
    }
    for (int i = 0; i < BB_MAX_NODES; i++) {
        if (!nodes[i].used) continue;
        if (nodes[i].parent != (uint16_t)nid) continue;
        char md[12];
        fmt_mode_ram(nodes[i].kind, md);
        vga_write(md, VGA_COLOR_LIGHT_GREY);
        vga_write(" 1 root root ", VGA_COLOR_LIGHT_GREY);
        char szb[16];
        int_to_str((int)nodes[i].data_len, szb);
        vga_write(szb, VGA_COLOR_LIGHT_CYAN);
        vga_write(" 0 ", VGA_COLOR_LIGHT_GREY);
        vga_write(nodes[i].name, VGA_COLOR_LIGHT_GREEN);
        if (nodes[i].kind == BB_KIND_DIR || nodes[i].kind == BB_KIND_MOUNT)
            vga_write("/", VGA_COLOR_LIGHT_CYAN);
        else if (nodes[i].kind == BB_KIND_SPECIAL)
            vga_write("@", VGA_COLOR_LIGHT_BROWN);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
}

int bb_cat(const char *path) {
    char abs[BB_PATH_MAX];
    bb_to_abs(bb_cwd, path, abs, sizeof(abs));
    if (looks_disk(abs)) {
        if (!fs_is_mounted()) {
            vga_write("cat: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
            return -1;
        }
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        fs_cat_path(e2);
        return 0;
    }
    int nid = bb_walk(abs);
    if (nid < 0) {
        vga_write("cat: not found\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (nodes[nid].kind == BB_KIND_DIR || nodes[nid].kind == BB_KIND_MOUNT) {
        vga_write("cat: is a directory\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (nodes[nid].kind == BB_KIND_SPECIAL) {
        if (nodes[nid].spec == SP_NONE) {
            vga_write("\n", VGA_COLOR_LIGHT_GREY);
            return 0;
        }
        special_cat(nodes[nid].spec);
        return 0;
    }
    uint32_t off = nodes[nid].data_off;
    uint32_t len = nodes[nid].data_len;
    for (uint32_t p = 0; p < len; p++) {
        char c = (char)pool[off + p];
        vga_putchar(c, VGA_COLOR_LIGHT_GREY);
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    return 0;
}

int bb_mkdir(const char *path) {
    char abs[BB_PATH_MAX];
    bb_to_abs(bb_cwd, path, abs, sizeof(abs));
    bb_normalize(abs);
    if (looks_disk(abs)) {
        vga_write("mkdir: cannot create under /disk here\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    char base[BB_NAME_MAX];
    char parent_path[BB_PATH_MAX];
    strncpy(parent_path, abs, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = 0;
    char *last_sl = NULL;
    for (char *q = parent_path; *q; q++)
        if (*q == '/') last_sl = q;
    if (!last_sl) return -1;
    if (last_sl == parent_path) {
        strncpy(base, last_sl + 1, BB_NAME_MAX - 1);
        base[BB_NAME_MAX - 1] = 0;
        parent_path[0] = '/';
        parent_path[1] = 0;
    } else {
        *last_sl = 0;
        strncpy(base, last_sl + 1, BB_NAME_MAX - 1);
        base[BB_NAME_MAX - 1] = 0;
    }
    bb_normalize(parent_path);
    int pin = bb_walk(parent_path);
    if (pin < 0 || nodes[pin].kind != BB_KIND_DIR) {
        vga_write("mkdir: bad parent\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (node_child(pin, base) >= 0) {
        vga_write("mkdir: exists\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (dir_put(pin, base) < 0) {
        vga_write("mkdir: ramfs full\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    return 0;
}

int bb_touch(const char *path) {
    char abs[BB_PATH_MAX];
    bb_to_abs(bb_cwd, path, abs, sizeof(abs));
    bb_normalize(abs);
    if (looks_disk(abs)) {
        vga_write("touch: cannot under /disk here\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    char base[BB_NAME_MAX];
    char pp[BB_PATH_MAX];
    strncpy(pp, abs, sizeof(pp) - 1);
    pp[sizeof(pp) - 1] = 0;
    char *last_sl = NULL;
    for (char *q = pp; *q; q++)
        if (*q == '/') last_sl = q;
    if (!last_sl) return -1;
    if (last_sl == pp) {
        strncpy(base, last_sl + 1, BB_NAME_MAX - 1);
        base[BB_NAME_MAX - 1] = 0;
        pp[0] = '/';
        pp[1] = 0;
    } else {
        *last_sl = 0;
        strncpy(base, last_sl + 1, BB_NAME_MAX - 1);
        base[BB_NAME_MAX - 1] = 0;
    }
    bb_normalize(pp);
    int pin = bb_walk(pp);
    if (pin < 0 || nodes[pin].kind != BB_KIND_DIR) return -1;
    int ex = node_child(pin, base);
    if (ex >= 0) return 0;
    if (file_put(pin, base, "", 0) < 0) return -1;
    return 0;
}

int bb_read_file_abs(const char *abs_in, uint8_t *buf, size_t cap, size_t *out_len) {
    char abs[BB_PATH_MAX];
    if (!abs_in || !buf || cap == 0) return -1;
    strncpy(abs, abs_in, sizeof(abs) - 1);
    abs[sizeof(abs) - 1] = 0;
    bb_normalize(abs);
    if (looks_disk(abs)) return -1;
    int nid = bb_walk(abs);
    if (nid < 0) return -1;
    if (nodes[nid].kind != BB_KIND_FILE) return -1;
    size_t n = nodes[nid].data_len;
    if (n > cap) n = cap;
    memcpy(buf, pool + nodes[nid].data_off, n);
    if (n < cap) buf[n] = 0;
    if (out_len) *out_len = n;
    return 0;
}

int bb_put_file_abs(const char *abs_in, const uint8_t *data, size_t len) {
    char abs[BB_PATH_MAX];
    if (!abs_in) return -1;
    strncpy(abs, abs_in, sizeof(abs) - 1);
    abs[sizeof(abs) - 1] = 0;
    bb_normalize(abs);
    if (looks_disk(abs)) return -1;
    char base[BB_NAME_MAX];
    char pp[BB_PATH_MAX];
    strncpy(pp, abs, sizeof(pp) - 1);
    pp[sizeof(pp) - 1] = 0;
    char *last_sl = NULL;
    for (char *q = pp; *q; q++)
        if (*q == '/') last_sl = q;
    if (!last_sl) return -1;
    if (last_sl == pp) {
        strncpy(base, last_sl + 1, BB_NAME_MAX - 1);
        base[BB_NAME_MAX - 1] = 0;
        pp[0] = '/';
        pp[1] = 0;
    } else {
        *last_sl = 0;
        strncpy(base, last_sl + 1, BB_NAME_MAX - 1);
        base[BB_NAME_MAX - 1] = 0;
    }
    bb_normalize(pp);
    int pin = bb_walk(pp);
    if (pin < 0 || nodes[pin].kind != BB_KIND_DIR) return -1;
    int ex = node_child(pin, base);
    if (ex >= 0) {
        if (nodes[ex].kind != BB_KIND_FILE) return -1;
        nodes[ex].used = 0;
    }
    if (pool_used + len > BB_POOL) return -1;
    if (file_put(pin, base, data, len) < 0) return -1;
    return 0;
}

int bb_mv_abs(const char *from_abs_in, const char *to_abs_in) {
    char from[BB_PATH_MAX], to[BB_PATH_MAX];
    strncpy(from, from_abs_in, sizeof(from) - 1);
    from[sizeof(from) - 1] = 0;
    strncpy(to, to_abs_in, sizeof(to) - 1);
    to[sizeof(to) - 1] = 0;
    bb_normalize(from);
    bb_normalize(to);
    if (looks_disk(from) || looks_disk(to)) return -1;
    uint8_t buf[8192];
    size_t n = 0;
    if (bb_read_file_abs(from, buf, sizeof(buf), &n) != 0) return -1;
    int src = bb_walk(from);
    if (src < 0 || nodes[src].kind != BB_KIND_FILE) return -1;
    if (bb_put_file_abs(to, buf, n) != 0) return -1;
    nodes[src].used = 0;
    return 0;
}

int bb_is_dir(const char *path) {
    char abs[BB_PATH_MAX];
    bb_to_abs(bb_cwd, path, abs, sizeof(abs));
    bb_normalize(abs);
    if (looks_disk(abs)) {
        if (!fs_is_mounted()) return 0;
        char e2[BB_PATH_MAX];
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        return fs_is_directory(e2);
    }
    int nid = bb_walk(abs);
    if (nid < 0) return 0;
    return nodes[nid].kind == BB_KIND_DIR || nodes[nid].kind == BB_KIND_MOUNT;
}

int bb_rm(const char *path, int is_dir) {
    char abs[BB_PATH_MAX];
    bb_to_abs(bb_cwd, path, abs, sizeof(abs));
    bb_normalize(abs);
    if (looks_disk(abs)) return -1;
    int nid = bb_walk(abs);
    if (nid <= 0) return -1;
    if (nodes[nid].kind == BB_KIND_MOUNT) return -1;
    if (is_dir) {
        if (nodes[nid].kind != BB_KIND_DIR) return -1;
        for (int i = 0; i < BB_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == (uint16_t)nid) return -1;
    } else {
        if (nodes[nid].kind == BB_KIND_DIR) return -1;
    }
    nodes[nid].used = 0;
    return 0;
}

void busybox_seed_ramfs(void) {
    ramfs_init();
    nodes[0].used = 1;
    nodes[0].parent = 0;
    nodes[0].name[0] = 0;
    nodes[0].kind = BB_KIND_DIR;
    nodes[0].spec = SP_NONE;

    int bin = dir_put(0, "bin");
    int sbin = dir_put(0, "sbin");
    int dev = dir_put(0, "dev");
    int proc = dir_put(0, "proc");
    int sys = dir_put(0, "sys");
    int lib = dir_put(0, "lib");
    int usr = dir_put(0, "usr");
    int etc = dir_put(0, "etc");
    int var = dir_put(0, "var");
    int opt = dir_put(0, "opt");
    int home = dir_put(0, "home");
    (void)dir_put(0, "root");
    int tmp = dir_put(0, "tmp");
    node_alloc(0, "disk", BB_KIND_MOUNT, SP_NONE);

    file_put(bin, "busybox",
        "YodaOS kernel BusyBox: all applets are built into the shell.\n"
        "Run: busybox --help   or   busybox <applet> [args]\n",
        strlen("YodaOS kernel BusyBox: all applets are built into the shell.\n"
               "Run: busybox --help   or   busybox <applet> [args]\n"));
    file_put(bin, "sh", "POSIX sh is the kernel shell ($>).\n", 37);
    file_put(sbin, "init", "pid1: kmain (kernel)\n", 21);
    spec_put(sbin, "halt", SP_SBIN_HALT);

    spec_put(dev, "null", SP_NONE);
    spec_put(dev, "zero", SP_DEV_ZERO);
    spec_put(dev, "urandom", SP_DEV_URANDOM);
    spec_put(dev, "tty0", SP_DEV_TTY);
    file_put(dev, "console", "char 5:1\n", 10);

    spec_put(proc, "cpuinfo", SP_PROC_CPUINFO);
    spec_put(proc, "meminfo", SP_PROC_MEMINFO);
    spec_put(proc, "version", SP_PROC_VERSION);
    spec_put(proc, "cmdline", SP_PROC_CMDLINE);
    spec_put(proc, "stat", SP_PROC_STAT);
    spec_put(proc, "uptime", SP_PROC_UPTIME);
    spec_put(proc, "mounts", SP_PROC_MOUNTS);

    int sys_kernel = dir_put(sys, "kernel");
    file_put(sys_kernel, "notes", "sysfs: export driver/kernel knobs (stub)\n", 44);
    spec_put(sys_kernel, "version", SP_SYS_KERNEL_VER);
    spec_put(sys_kernel, "release", SP_SYS_KERNEL_REL);
    int sys_class = dir_put(sys, "class");
    (void)dir_put(sys_class, "tty");
    (void)dir_put(sys_class, "net");

    file_put(lib, "ld-linux-x86-64.so.2", "ELF dynamic linker (stub)\n", 27);
    int mod = dir_put(lib, "modules");
    int modver = dir_put(mod, "0.0-yoda");
    file_put(modver, "modules.builtin", "kernel/fs/ext2/ext2.ko\n", 22);

    int usrbin = dir_put(usr, "bin");
    int usrsbin = dir_put(usr, "sbin");
    file_put(usrbin, "true", "", 0);
    file_put(usrsbin, "chroot", "chroot: use cd /disk for ext2\n", 32);

    file_put(etc, "passwd", "root:x:0:0:root:/root:/bin/sh\n", 29);
    file_put(etc, "hostname", "yodaos\n", 7);
    file_put(etc, "fstab", "ramfs / ramfs defaults 0 0\next2 /disk ext2 rw 0 0\n", 50);
    file_put(etc, "issue", "YodaOS 1.2 — type help -p\n", 27);
    {
        static const char profile_txt[] = "# /etc/profile\necho sourced /etc/profile\n";
        file_put(etc, "profile", profile_txt, sizeof(profile_txt) - 1);
    }

    int vlog = dir_put(var, "log");
    int vrun = dir_put(var, "run");
    (void)dir_put(var, "tmp");
    file_put(vlog, "messages", "Apr 12 00:00:01 klogd: ramfs online\n", 36);
    file_put(vrun, "utmp", "(session table not implemented)\n", 34);

    file_put(opt, "README", "/opt: add-on payloads (package staging)\n", 41);

    int hu = dir_put(home, "user");
    file_put(hu, "welcome.txt", "Your home on ramfs. Files here are volatile.\n", 46);

    file_put(tmp, ".keep", "", 0);
}
