#include "bb_applets.h"
#include "kernel.h"
#include "drivers.h"
#include "fs.h"
#include "ramfs.h"
#include "string.h"
#include "shell.h"

static int path_disk_prefix(const char *bb_abs) {
    return strncmp(bb_abs, "/disk", 5) == 0 && (bb_abs[5] == 0 || bb_abs[5] == '/');
}

static void hybrid_abs(const char *path, char *out_abs, size_t olen) {
    char cwd[256];
    bb_get_cwd(cwd, sizeof(cwd));
    bb_to_abs(cwd, path, out_abs, olen);
}

int bb_tokenize(const char *line, char *argv[], int max_args, char *pool, size_t pool_sz) {
    int argc = 0;
    size_t pi = 0;
    const char *p = line;
    if (!line || !argv || !pool || max_args < 2 || pool_sz < 2)
        return 0;
    while (*p == ' ' || *p == '\t')
        p++;
    while (*p && argc < max_args - 1) {
        if (pi + 1 >= pool_sz)
            break;
        argv[argc++] = pool + pi;
        while (*p && *p != ' ' && *p != '\t') {
            if (pi + 1 >= pool_sz)
                goto done;
            pool[pi++] = *p++;
        }
        if (pi >= pool_sz)
            break;
        pool[pi++] = 0;
        while (*p == ' ' || *p == '\t')
            p++;
    }
done:
    argv[argc] = 0;
    return argc;
}

int bb_hybrid_read(const char *path, uint8_t *buf, size_t cap, size_t *out_len) {
    char abs[256], e2[256];
    hybrid_abs(path, abs, sizeof(abs));
    if (path_disk_prefix(abs)) {
        if (!fs_is_mounted())
            return -1;
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        uint32_t sz = 0;
        if (fs_open(e2, buf, (uint32_t)cap, &sz) != 0)
            return -1;
        if (out_len) *out_len = (size_t)sz;
        return 0;
    }
    return bb_read_file_abs(abs, buf, cap, out_len);
}

int bb_hybrid_write(const char *path, const uint8_t *data, size_t len) {
    char abs[256], e2[256];
    hybrid_abs(path, abs, sizeof(abs));
    if (path_disk_prefix(abs)) {
        if (!fs_is_mounted())
            return -1;
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        if (!fs_exists(e2)) {
            if (fs_create(e2, 0) != 0)
                return -1;
        }
        return fs_write(e2, data, (uint32_t)len);
    }
    return bb_put_file_abs(abs, data, len);
}

static int bb_atoi(const char *s) {
    int v = 0, neg = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

static unsigned rtc_total_s(void) {
    int h, m, s;
    get_rtc_time(&h, &m, &s);
    return (unsigned)h * 3600u + (unsigned)m * 60u + (unsigned)s;
}

static void sleep_seconds(unsigned sec) {
    unsigned t0 = rtc_total_s();
    for (;;) {
        unsigned t = rtc_total_s();
        unsigned d = t >= t0 ? t - t0 : (86400u - t0) + t;
        if (d >= sec) break;
    }
}

static int mkdir_p_ext2(char *path) {
    size_t len = strlen(path);
    if (len < 2)
        return 0;
    for (size_t i = 1; i < len; i++) {
        if (path[i] != '/')
            continue;
        char c = path[i];
        path[i] = 0;
        if (strlen(path) > 1 && !fs_exists(path)) {
            if (fs_mkdir(path) != 0) {
                path[i] = c;
                return -1;
            }
        }
        path[i] = c;
    }
    if (!fs_exists(path))
        return fs_mkdir(path);
    return 0;
}

static int mkdir_p_ram(const char *abs_norm) {
    char work[256];
    strncpy(work, abs_norm, sizeof(work) - 1);
    work[sizeof(work) - 1] = 0;
    if (work[0] != '/')
        return -1;
    size_t L = strlen(work);
    if (L <= 1)
        return 0;
    for (size_t i = 1; i < L; i++) {
        if (work[i] != '/')
            continue;
        char save = work[i];
        work[i] = 0;
        if (bb_lookup_path(work) < 0) {
            if (bb_mkdir(work) != 0) {
                work[i] = save;
                return -1;
            }
        }
        work[i] = save;
    }
    if (bb_lookup_path(work) < 0)
        return bb_mkdir(work);
    return 0;
}

int bb_kernel_dispatch(int argc, char **argv) {
    if (argc < 1 || !argv[0])
        return 0;
    const char *a0 = argv[0];

    if (strcmp(a0, "reboot") == 0) {
        cmd_reboot();
        return 1;
    }
    if (strcmp(a0, "shutdown") == 0) {
        cmd_shutdown();
        return 1;
    }
    if (strcmp(a0, "sysinf") == 0) {
        cmd_sysinf();
        return 1;
    }
    if (strcmp(a0, "clear") == 0) {
        cmd_clear();
        return 1;
    }
    if (strcmp(a0, "asciiart") == 0) {
        cmd_asciiart();
        return 1;
    }
    if (strcmp(a0, "cpuid") == 0) {
        cmd_cpuid();
        return 1;
    }
    if (strcmp(a0, "memtest") == 0) {
        cmd_memtest();
        return 1;
    }
    if (strcmp(a0, "rand") == 0) {
        cmd_rand();
        return 1;
    }
    if (strcmp(a0, "pause") == 0) {
        cmd_pause();
        return 1;
    }
    if (strcmp(a0, "format") == 0) {
        cmd_format();
        return 1;
    }
    if (strcmp(a0, "ring3test") == 0) {
        vga_write("Entering ring3 demo...\n", VGA_COLOR_LIGHT_CYAN);
        extern uint8_t user_ring3_stack[];
        jump_user_ring3((uint64_t)(uintptr_t)ring3_demo_entry, (uint64_t)(uintptr_t)(user_ring3_stack + 4096));
        return 1;
    }
    if (strcmp(a0, "panic_test") == 0) {
        vga_write("kernel panic stack test (halt loop)\n", VGA_COLOR_LIGHT_RED);
        kernel_panic_stack_test();
        return 1;
    }
    if (strcmp(a0, "date") == 0) {
        int d = 0, t = 0;
        for (int i = 1; i < argc; i++) {
            if (strstr(argv[i], "-d")) d = 1;
            if (strstr(argv[i], "-t")) t = 1;
        }
        if (!d && !t) {
            d = 1;
            t = 1;
        }
        cmd_date(d, t);
        return 1;
    }
    if (strcmp(a0, "help") == 0 && argc >= 2 && strncmp(argv[1], "-p", 2) == 0) {
        int page = 1;
        if (argc >= 3) page = bb_atoi(argv[2]);
        if (page < 1) page = 1;
        cmd_help_p(page);
        return 1;
    }
    if (strcmp(a0, "ynan") == 0) {
        if (argc < 2) {
            vga_write("Usage: ynan <file>\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        char abs[256], e2[256];
        hybrid_abs(argv[1], abs, sizeof(abs));
        if (!path_disk_prefix(abs)) {
            vga_write("ynan: use a path under /disk/... (ext2)\n", VGA_COLOR_LIGHT_BROWN);
            return 1;
        }
        if (!fs_is_mounted()) {
            vga_write("ynan: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        bb_disk_to_ext2(abs, e2, sizeof(e2));
        cmd_ynan_at(e2);
        return 1;
    }
    if (strcmp(a0, "sh") == 0 && argc >= 2) {
        shell_run_script(argv[1]);
        return 1;
    }
    if (strcmp(a0, "source") == 0 && argc >= 2) {
        shell_run_script(argv[1]);
        return 1;
    }
    if (strcmp(a0, ".") == 0 && argc >= 2) {
        shell_run_script(argv[1]);
        return 1;
    }
    if (strcmp(a0, "exec") == 0) {
        if (argc < 2)
            vga_write("Usage: exec <filename>\n", VGA_COLOR_LIGHT_RED);
        else
            vga_write("exec: not implemented\n", VGA_COLOR_LIGHT_BROWN);
        return 1;
    }
    return 0;
}

static int applet_ls(int argc, char **argv) {
    int long_fmt = 0, show_all = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *o = argv[i] + 1;
        if (*o == 0) break;
        for (; *o; o++) {
            if (*o == 'l') long_fmt = 1;
            else if (*o == 'a') show_all = 1;
        }
    }
    if (i >= argc) {
        if (long_fmt)
            bb_list_long_cwd(show_all);
        else
            bb_list_cwd();
        return 1;
    }
    for (; i < argc; i++) {
        char abs[256];
        hybrid_abs(argv[i], abs, sizeof(abs));
        if (long_fmt)
            bb_list_long_abs(abs, show_all);
        else
            bb_list_abs(abs);
        if (i + 1 < argc) vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
    return 1;
}

static int mv_one(const char *from, const char *to) {
    char fa[256], ta[256];
    hybrid_abs(from, fa, sizeof(fa));
    hybrid_abs(to, ta, sizeof(ta));
    int fd = path_disk_prefix(fa), td = path_disk_prefix(ta);
    if (fd && td) {
        if (!fs_is_mounted()) return -1;
        char ef[256], et[256];
        bb_disk_to_ext2(fa, ef, sizeof(ef));
        bb_disk_to_ext2(ta, et, sizeof(et));
        if (fs_is_directory(ef) || fs_is_directory(et)) return -2;
        return fs_mv_path(ef, et);
    }
    if (!fd && !td) {
        if (bb_is_dir(from) || bb_is_dir(to)) return -2;
        return bb_mv_abs(fa, ta);
    }
    if (bb_is_dir(from) || bb_is_dir(to)) return -2;
    uint8_t buf[8192];
    size_t n = 0;
    if (bb_hybrid_read(from, buf, sizeof(buf), &n) != 0) return -1;
    if (bb_hybrid_write(to, buf, n) != 0) return -1;
    if (fd) {
        char ef[256];
        bb_disk_to_ext2(fa, ef, sizeof(ef));
        if (fs_is_directory(ef)) return -2;
        return fs_delete(ef);
    }
    return bb_rm(from, 0);
}

static int applet_wc(int argc, char **argv) {
    int lines = 1, words = 1, bytes = 1, fi = 1;
    if (argc >= 2 && argv[1][0] == '-') {
        const char *o = argv[1] + 1;
        if (*o) {
            lines = words = bytes = 0;
            for (; *o; o++) {
                if (*o == 'l') lines = 1;
                if (*o == 'w') words = 1;
                if (*o == 'c') bytes = 1;
            }
            fi = 2;
        }
    }
    if (fi >= argc) {
        vga_write("wc: need file\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    uint8_t buf[8192];
    size_t n = 0;
    if (bb_hybrid_read(argv[fi], buf, sizeof(buf), &n) != 0) {
        vga_write("wc: read failed\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    int lc = 0, wc = 0, inw = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == '\n') lc++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inw) {
                wc++;
                inw = 0;
            }
        } else {
            inw = 1;
        }
    }
    if (inw) wc++;
    char tmp[16];
    if (lines) {
        int_to_str(lc, tmp);
        vga_write(tmp, VGA_COLOR_LIGHT_GREY);
        vga_write(" ", VGA_COLOR_LIGHT_GREY);
    }
    if (words) {
        int_to_str(wc, tmp);
        vga_write(tmp, VGA_COLOR_LIGHT_GREY);
        vga_write(" ", VGA_COLOR_LIGHT_GREY);
    }
    if (bytes) {
        int_to_str((int)n, tmp);
        vga_write(tmp, VGA_COLOR_LIGHT_GREY);
        vga_write(" ", VGA_COLOR_LIGHT_GREY);
    }
    vga_write(argv[fi], VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    return 1;
}

static int applet_head_tail(int argc, char **argv, int do_tail) {
    int nlines = 10, fi = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        nlines = bb_atoi(argv[2]);
        if (nlines < 1) nlines = 1;
        fi = 3;
    }
    if (fi >= argc) {
        vga_write("head/tail: need file\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    uint8_t buf[8192];
    size_t n = 0;
    if (bb_hybrid_read(argv[fi], buf, sizeof(buf), &n) != 0) {
        vga_write("head/tail: read failed\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    if (!do_tail) {
        int left = nlines;
        for (size_t i = 0; i < n && left > 0; i++) {
            vga_putchar((char)buf[i], VGA_COLOR_LIGHT_GREY);
            if (buf[i] == '\n') left--;
        }
        return 1;
    }
    int starts[256];
    int ns = 0;
    starts[ns++] = 0;
    for (size_t i = 0; i < n && ns < 255; i++) {
        if (buf[i] == '\n')
            starts[ns++] = (int)i + 1;
    }
    int total = ns;
    int first = total - nlines;
    if (first < 0) first = 0;
    size_t beg = (size_t)starts[first];
    for (size_t i = beg; i < n; i++)
        vga_putchar((char)buf[i], VGA_COLOR_LIGHT_GREY);
    return 1;
}

static int applet_grep(int argc, char **argv) {
    if (argc < 3) {
        vga_write("Usage: grep <pattern> <file>\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    const char *pat = argv[1];
    uint8_t buf[8192];
    size_t n = 0;
    if (bb_hybrid_read(argv[2], buf, sizeof(buf), &n) != 0) {
        vga_write("grep: read failed\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    size_t plen = strlen(pat);
    int line_start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n') {
            int hit = 0;
            int linlen = (int)i - line_start;
            if (plen == 0)
                hit = 1;
            else {
                for (int j = 0; j <= linlen - (int)plen; j++) {
                    if (memcmp(buf + line_start + j, pat, plen) == 0) {
                        hit = 1;
                        break;
                    }
                }
            }
            if (hit) {
                for (int k = line_start; k < (int)i; k++)
                    vga_putchar((char)buf[k], VGA_COLOR_LIGHT_GREY);
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
            }
            line_start = (int)i + 1;
        }
    }
    return 1;
}

static void print_basename(const char *path, const char *suf) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') base = p + 1;
    if (suf && *suf) {
        size_t bl = strlen(base), sl = strlen(suf);
        if (bl >= sl && strcmp(base + bl - sl, suf) == 0) {
            for (size_t i = 0; i < bl - sl; i++)
                vga_putchar(base[i], VGA_COLOR_LIGHT_GREY);
            vga_write("\n", VGA_COLOR_LIGHT_GREY);
            return;
        }
    }
    vga_write(base, VGA_COLOR_LIGHT_GREY);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void print_dirname(const char *path) {
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char *last = 0;
    for (char *p = tmp; *p; p++)
        if (*p == '/') last = p;
    if (!last) {
        vga_write(".\n", VGA_COLOR_LIGHT_GREY);
        return;
    }
    if (last == tmp) {
        vga_write("/\n", VGA_COLOR_LIGHT_GREY);
        return;
    }
    *last = 0;
    if (tmp[0] == 0) {
        vga_write("/\n", VGA_COLOR_LIGHT_GREY);
        return;
    }
    vga_write(tmp, VGA_COLOR_LIGHT_GREY);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

int bb_applet_main(int argc, char **argv) {
    if (argc < 1 || !argv[0])
        return 0;
    const char *cmd = argv[0];

    if (strcmp(cmd, "busybox") == 0) {
        if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
            vga_write("BusyBox multi-call (kernel): ls cd pwd cat echo mkdir rm rmdir cp mv touch\n", VGA_COLOR_LIGHT_CYAN);
            vga_write("sync uname wc head tail basename dirname grep true false sleep\n", VGA_COLOR_LIGHT_CYAN);
            vga_write("Use: busybox <applet> [args]\n", VGA_COLOR_LIGHT_CYAN);
            return 1;
        }
        if (argc >= 2)
            return bb_applet_main(argc - 1, argv + 1);
        return 1;
    }

    if (strcmp(cmd, "ls") == 0) return applet_ls(argc, argv);
    if (strcmp(cmd, "cd") == 0) {
        if (argc < 2)
            bb_chdir("/");
        else
            bb_chdir(argv[1]);
        return 1;
    }
    if (strcmp(cmd, "pwd") == 0) {
        char b[256];
        bb_get_cwd(b, sizeof(b));
        vga_write(b, VGA_COLOR_LIGHT_GREY);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
        return 1;
    }
    if (strcmp(cmd, "cat") == 0) {
        if (argc < 2) {
            vga_write("cat: need file\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        for (int i = 1; i < argc; i++) bb_cat(argv[i]);
        return 1;
    }
    if (strcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) vga_write(" ", VGA_COLOR_LIGHT_GREY);
            vga_write(argv[i], VGA_COLOR_LIGHT_GREY);
        }
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
        return 1;
    }
    if (strcmp(cmd, "mkdir") == 0) {
        int pflag = 0, ai = 1;
        for (; ai < argc && argv[ai][0] == '-'; ai++) {
            if (strcmp(argv[ai], "-p") == 0) pflag = 1;
        }
        if (ai >= argc) {
            vga_write("mkdir: need path\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        for (; ai < argc; ai++) {
            char abs[256];
            hybrid_abs(argv[ai], abs, sizeof(abs));
            if (path_disk_prefix(abs)) {
                if (!fs_is_mounted()) {
                    vga_write("mkdir: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
                    return 1;
                }
                char e2[256], work[256];
                bb_disk_to_ext2(abs, e2, sizeof(e2));
                strncpy(work, e2, sizeof(work) - 1);
                work[sizeof(work) - 1] = 0;
                if (pflag) {
                    if (mkdir_p_ext2(work) != 0)
                        vga_write("mkdir: failed (ext2)\n", VGA_COLOR_LIGHT_RED);
                } else {
                    if (fs_mkdir(e2) != 0)
                        vga_write("mkdir: failed (ext2)\n", VGA_COLOR_LIGHT_RED);
                }
            } else {
                if (pflag) {
                    if (mkdir_p_ram(abs) != 0)
                        vga_write("mkdir: failed (ramfs)\n", VGA_COLOR_LIGHT_RED);
                } else {
                    if (bb_mkdir(argv[ai]) != 0)
                        vga_write("mkdir: failed (ramfs)\n", VGA_COLOR_LIGHT_RED);
                }
            }
        }
        return 1;
    }
    if (strcmp(cmd, "rm") == 0) {
        if (argc < 3) {
            vga_write("Usage: rm -d <dir> | rm -f <file>\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        const char *flag = argv[1];
        const char *name = argv[2];
        char abs[256], e2[256];
        hybrid_abs(name, abs, sizeof(abs));
        if (strcmp(flag, "-d") == 0) {
            if (path_disk_prefix(abs)) {
                if (!fs_is_mounted())
                    vga_write("rm: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
                else {
                    bb_disk_to_ext2(abs, e2, sizeof(e2));
                    if (!fs_is_directory(e2))
                        vga_write("rm: not a directory\n", VGA_COLOR_LIGHT_RED);
                    else if (fs_delete(e2) != 0)
                        vga_write("rm: cannot remove directory\n", VGA_COLOR_LIGHT_RED);
                }
            } else {
                if (!bb_is_dir(name))
                    vga_write("rm: not a directory\n", VGA_COLOR_LIGHT_RED);
                else if (bb_rm(name, 1) != 0)
                    vga_write("rm: cannot remove directory\n", VGA_COLOR_LIGHT_RED);
            }
        } else if (strcmp(flag, "-f") == 0) {
            if (path_disk_prefix(abs)) {
                if (!fs_is_mounted())
                    vga_write("rm: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
                else {
                    bb_disk_to_ext2(abs, e2, sizeof(e2));
                    if (fs_is_directory(e2))
                        vga_write("rm: is a directory (use rm -d)\n", VGA_COLOR_LIGHT_RED);
                    else if (fs_delete(e2) != 0)
                        vga_write("rm: cannot remove\n", VGA_COLOR_LIGHT_RED);
                }
            } else {
                if (bb_is_dir(name))
                    vga_write("rm: is a directory (use rm -d)\n", VGA_COLOR_LIGHT_RED);
                else if (bb_rm(name, 0) != 0)
                    vga_write("rm: cannot remove\n", VGA_COLOR_LIGHT_RED);
            }
        } else
            vga_write("Usage: rm -d <dir> | rm -f <file>\n", VGA_COLOR_LIGHT_RED);
        return 1;
    }
    if (strcmp(cmd, "rmdir") == 0) {
        if (argc < 2) {
            vga_write("Usage: rmdir <dir>\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        char *fakev[4];
        fakev[0] = (char *)"rm";
        fakev[1] = (char *)"-d";
        fakev[2] = argv[1];
        fakev[3] = 0;
        return bb_applet_main(3, fakev);
    }
    if (strcmp(cmd, "cp") == 0) {
        if (argc < 3) {
            vga_write("Usage: cp <src> <dst>\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        uint8_t buf[8192];
        size_t n = 0;
        if (bb_hybrid_read(argv[1], buf, sizeof(buf), &n) != 0)
            vga_write("cp: read failed\n", VGA_COLOR_LIGHT_RED);
        else if (bb_hybrid_write(argv[2], buf, n) != 0)
            vga_write("cp: write failed\n", VGA_COLOR_LIGHT_RED);
        else
            vga_write("cp: ok\n", VGA_COLOR_LIGHT_GREEN);
        return 1;
    }
    if (strcmp(cmd, "mv") == 0) {
        if (argc != 3) {
            vga_write("Usage: mv <src> <dst>\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        int r = mv_one(argv[1], argv[2]);
        if (r == -2)
            vga_write("mv: directories or cross-layout not supported\n", VGA_COLOR_LIGHT_RED);
        else if (r != 0)
            vga_write("mv: failed\n", VGA_COLOR_LIGHT_RED);
        else
            vga_write("mv: ok\n", VGA_COLOR_LIGHT_GREEN);
        return 1;
    }
    if (strcmp(cmd, "touch") == 0) {
        if (argc < 2) {
            vga_write("touch: need file\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        for (int i = 1; i < argc; i++) {
            char abs[256], e2[256];
            hybrid_abs(argv[i], abs, sizeof(abs));
            if (path_disk_prefix(abs)) {
                if (!fs_is_mounted()) {
                    vga_write("touch: ext2 not mounted\n", VGA_COLOR_LIGHT_RED);
                    return 1;
                }
                bb_disk_to_ext2(abs, e2, sizeof(e2));
                if (fs_exists(e2))
                    continue;
                if (fs_create(e2, 0) != 0)
                    vga_write("touch: failed\n", VGA_COLOR_LIGHT_RED);
            } else {
                if (bb_touch(argv[i]) != 0)
                    vga_write("touch: failed\n", VGA_COLOR_LIGHT_RED);
            }
        }
        return 1;
    }
    if (strcmp(cmd, "sync") == 0) {
        fs_sync_to_disk();
        ata_flush();
        return 1;
    }
    if (strcmp(cmd, "uname") == 0) {
        int all = 0;
        if (argc >= 2 && strcmp(argv[1], "-a") == 0) all = 1;
        if (all)
            vga_write("YodaOS yoda 0.0 #1 x86_64 YodaOS\n", VGA_COLOR_LIGHT_GREY);
        else
            vga_write("YodaOS\n", VGA_COLOR_LIGHT_GREY);
        return 1;
    }
    if (strcmp(cmd, "wc") == 0) return applet_wc(argc, argv);
    if (strcmp(cmd, "head") == 0) return applet_head_tail(argc, argv, 0);
    if (strcmp(cmd, "tail") == 0) return applet_head_tail(argc, argv, 1);
    if (strcmp(cmd, "basename") == 0) {
        if (argc < 2) {
            vga_write("basename: need path\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        print_basename(argv[1], argc >= 3 ? argv[2] : 0);
        return 1;
    }
    if (strcmp(cmd, "dirname") == 0) {
        if (argc < 2) {
            vga_write("dirname: need path\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        print_dirname(argv[1]);
        return 1;
    }
    if (strcmp(cmd, "grep") == 0) return applet_grep(argc, argv);
    if (strcmp(cmd, "true") == 0) return 1;
    if (strcmp(cmd, "false") == 0) return 1;
    if (strcmp(cmd, "sleep") == 0) {
        if (argc < 2) {
            vga_write("sleep: need seconds\n", VGA_COLOR_LIGHT_RED);
            return 1;
        }
        unsigned sec = (unsigned)bb_atoi(argv[1]);
        if (sec > 3600) sec = 3600;
        sleep_seconds(sec);
        return 1;
    }
    return 0;
}