#include "shell.h"
#include "kernel.h"
#include "drivers.h"
#include "fs.h"
#include "ramfs.h"
#include "bb_applets.h"
#include "string.h"

#define HISTORY_SIZE 10
static char history[HISTORY_SIZE][64];
static int history_count;
static int history_index;

static const char *skip_ws(const char *p) {
    while (*p == ' ') p++;
    return p;
}

static int read_script_file(const char *path, uint8_t *buf, size_t cap, size_t *out_len) {
    if (cap < 2)
        return -1;
    if (bb_hybrid_read(path, buf, cap - 1, out_len) != 0)
        return -1;
    if (*out_len < cap)
        buf[*out_len] = 0;
    else
        buf[cap - 1] = 0;
    return 0;
}

static int g_script_depth = 0;

void shell_run_script(const char *path) {
    if (g_script_depth >= 6) {
        vga_write("sh: nesting limit\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint8_t buf[4096];
    size_t n = 0;
    if (read_script_file(path, buf, sizeof(buf), &n) != 0) {
        vga_write("sh: cannot read ", VGA_COLOR_LIGHT_RED);
        vga_write(path, VGA_COLOR_LIGHT_RED);
        vga_write("\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    buf[n] = 0;
    g_script_depth++;
    char *line = (char *)buf;
    while (*line) {
        char *e = line;
        while (*e && *e != '\n' && *e != '\r') e++;
        char save = *e;
        *e = 0;
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (*t && *t != '#') {
            if (strncmp(t, "echo ", 5) == 0) {
                vga_write(t + 5, VGA_COLOR_LIGHT_GREY);
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
            } else if (strcmp(t, "echo") == 0) {
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
            } else if (strncmp(t, "export ", 7) == 0) {
                (void)t;
            } else if (strncmp(t, "sleep ", 6) == 0) {
                (void)t;
            } else if (strcmp(t, "true") == 0 || strcmp(t, ":") == 0) {
            } else if (strcmp(t, "false") == 0) {
            } else if (strncmp(t, "sh ", 3) == 0) {
                shell_run_script(skip_ws(t + 3));
            } else if (strncmp(t, "source ", 7) == 0) {
                shell_run_script(skip_ws(t + 7));
            } else
                shell_dispatch_line(t);
        }
        *e = save;
        if (!*e) break;
        line = e + 1;
        if (save == '\r' && *line == '\n') line++;
    }
    g_script_depth--;
}

void shell_dispatch_line(const char *cmd) {
    if (!cmd || cmd[0] == '\0') {
        vga_taskbar_refresh();
        return;
    }

    char pool[BB_ARGPOOL];
    char *argv[BB_MAX_ARGS];
    int argc = bb_tokenize(cmd, argv, BB_MAX_ARGS, pool, sizeof(pool));

    if (argc == 0) {
        vga_taskbar_refresh();
        return;
    }

    if (!bb_kernel_dispatch(argc, argv) && !bb_applet_main(argc, argv))
        vga_write("Unknown command\n", VGA_COLOR_LIGHT_RED);

    fs_sync_to_disk();
    vga_taskbar_refresh();
}

void shell_run(void) {
    char cmd[64];
    int cmd_pos = 0;
    memset(history, 0, sizeof(history));
    history_count = 0;
    history_index = -1;

    vga_taskbar_refresh();
    fs_ensure_mounted();
    if (fs_is_mounted()) {
        bb_chdir("/disk");
        vga_write("(cwd /disk — ext2; cd / для ramfs)\n", VGA_COLOR_LIGHT_CYAN);
    }
    vga_write("\n$> ", VGA_COLOR_LIGHT_GREEN);
    while (1) {
        uint8_t sc = wait_for_key();
        if (sc == 0xE0) {
            uint8_t sc2 = wait_for_key();
            if (sc2 == 0x48) {
                if (history_count > 0 && history_index < history_count - 1) {
                    history_index++;
                    while (cmd_pos > 0) {
                        cmd_pos--;
                        vga_putchar('\b', VGA_COLOR_LIGHT_GREY);
                    }
                    strcpy(cmd, history[history_count - 1 - history_index]);
                    cmd_pos = (int)strlen(cmd);
                    vga_write(cmd, VGA_COLOR_LIGHT_GREY);
                }
            } else if (sc2 == 0x50) {
                if (history_index > 0) {
                    history_index--;
                    while (cmd_pos > 0) {
                        cmd_pos--;
                        vga_putchar('\b', VGA_COLOR_LIGHT_GREY);
                    }
                    strcpy(cmd, history[history_count - 1 - history_index]);
                    cmd_pos = (int)strlen(cmd);
                    vga_write(cmd, VGA_COLOR_LIGHT_GREY);
                } else if (history_index == 0) {
                    history_index = -1;
                    while (cmd_pos > 0) {
                        cmd_pos--;
                        vga_putchar('\b', VGA_COLOR_LIGHT_GREY);
                    }
                    cmd[0] = '\0';
                    cmd_pos = 0;
                }
            }
            continue;
        }

        char c = scancode_to_char(sc);
        if (c == '\n' || c == '\r') {
            cmd[cmd_pos] = '\0';
            vga_write("\n", VGA_COLOR_LIGHT_GREY);

            if (cmd[0] != '\0') {
                if (history_count == 0 || strcmp(history[history_count - 1], cmd) != 0) {
                    if (history_count < HISTORY_SIZE) {
                        strcpy(history[history_count], cmd);
                        history_count++;
                    } else {
                        for (int i = 0; i < HISTORY_SIZE - 1; i++)
                            strcpy(history[i], history[i + 1]);
                        strcpy(history[HISTORY_SIZE - 1], cmd);
                    }
                }
            }
            history_index = -1;

            shell_dispatch_line(cmd);

            cmd_pos = 0;
            vga_write("$> ", VGA_COLOR_LIGHT_GREEN);
        } else if (c == '\b') {
            if (cmd_pos > 0) {
                cmd_pos--;
                vga_putchar('\b', VGA_COLOR_LIGHT_GREY);
            }
        } else if (c >= ' ' && c <= '~' && cmd_pos < 63) {
            cmd[cmd_pos++] = c;
            vga_putchar(c, VGA_COLOR_LIGHT_GREY);
        }
    }
}
