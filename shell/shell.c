#include "shell.h"
#include "kernel.h"
#include "drivers.h"
#include "fs.h"
#include "string.h"
#include "types.h"

extern char getchar(void);
extern uint32_t block_size;

#define HISTORY_SIZE 10
static char history[HISTORY_SIZE][64];
static int history_count;
static int history_index;

static char shell_cwd[256] = "/";

// Вспомогательные функции
static void shell_update_cwd(void) {
    fs_get_cwd(shell_cwd, sizeof(shell_cwd));
}

static const char *base_name(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

static int shell_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}


static void path_join(char *out, size_t out_sz, const char *cwd, const char *rel) {
    out[0] = 0;
    if (cwd[0] == '/' && cwd[1] == 0) {
        strncat(out, "/", out_sz - 1);
        strncat(out, rel, out_sz - strlen(out) - 1);
    } else {
        strncat(out, cwd, out_sz - 1);
        strncat(out, "/", out_sz - strlen(out) - 1);
        strncat(out, rel, out_sz - strlen(out) - 1);
    }
    out[out_sz - 1] = 0;
}

static int change_dir(const char *path) {
    if (fs_cd(path) == 0) {
        shell_update_cwd();
        return 0;
    }
    return -1;
}

// ------------------------------------------------------------
// Команды (имена уникальные, без конфликта с fs.h)
// ------------------------------------------------------------
static void do_ls(int argc, char **argv) {
    int long_fmt = 0, show_all = 0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *o = argv[i] + 1;
        for (; *o; o++) {
            if (*o == 'l') long_fmt = 1;
            else if (*o == 'a') show_all = 1;
        }
    }
    if (i >= argc) {
        if (long_fmt)
            fs_list_long_at_path(shell_cwd, show_all);
        else
            fs_list_at_path(shell_cwd);
        return;
    }
    for (; i < argc; i++) {
        char *path = argv[i];
        char full[256];
        if (path[0] != '/') {
            path_join(full, sizeof(full), shell_cwd, path);
            path = full;
        }
        if (long_fmt)
            fs_list_long_at_path(path, show_all);
        else
            fs_list_at_path(path);
        if (i + 1 < argc) vga_write("\n", VGA_COLOR_LIGHT_GREY);
    }
}

static void do_cd(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : "/";
    if (change_dir(path) != 0)
        vga_write("cd: failed\n", VGA_COLOR_LIGHT_RED);
}

static void do_pwd(void) {
    char buf[256];
    fs_get_cwd(buf, sizeof(buf));
    vga_write(buf, VGA_COLOR_LIGHT_GREY);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void do_mkdir(int argc, char **argv) {
    int pflag = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-'; ai++) {
        if (strcmp(argv[ai], "-p") == 0) pflag = 1;
    }
    if (ai >= argc) {
        vga_write("mkdir: need path\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    for (; ai < argc; ai++) {
        char *path = argv[ai];
        char full[256];
        if (path[0] != '/') {
            path_join(full, sizeof(full), shell_cwd, path);
            path = full;
        }
        if (pflag) {
            char tmp[256];
            strcpy(tmp, path);
            size_t len = strlen(tmp);
            for (size_t j = 1; j < len; j++) {
                if (tmp[j] == '/') {
                    tmp[j] = 0;
                    if (!fs_exists(tmp))
                        fs_mkdir(tmp);
                    tmp[j] = '/';
                }
            }
        }
        if (fs_mkdir(path) != 0)
            vga_write("mkdir: failed\n", VGA_COLOR_LIGHT_RED);
    }
}

static void do_rmdir(int argc, char **argv) {
    if (argc < 2) {
        vga_write("rmdir: need path\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    for (int i = 1; i < argc; i++) {
        char *path = argv[i];
        char full[256];
        if (path[0] != '/') {
            path_join(full, sizeof(full), shell_cwd, path);
            path = full;
        }
        if (fs_delete(path) != 0)
            vga_write("rmdir: failed\n", VGA_COLOR_LIGHT_RED);
    }
}
static void do_rm(int argc, char **argv) {
    int recursive = 0;   // -r
    int force = 0;       // -f
    int verbose = 0;     // -v
    int interactive = 0; // -i
    int dir_only = 0;    // -d (удалять пустые директории)

    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *opt = argv[i] + 1;
        if (opt[0] == '-') break; // --
        while (*opt) {
            switch (*opt) {
                case 'r': recursive = 1; break;
                case 'f': force = 1; break;
                case 'v': verbose = 1; break;
                case 'i': interactive = 1; break;
                case 'd': dir_only = 1; break;
                default:
                    vga_write("rm: invalid option -- '", VGA_COLOR_LIGHT_RED);
                    vga_putchar(*opt, VGA_COLOR_LIGHT_RED);
                    vga_write("'\n", VGA_COLOR_LIGHT_RED);
                    vga_write("Usage: rm [-r] [-f] [-v] [-i] [-d] file...\n", VGA_COLOR_LIGHT_RED);
                    return;
            }
            opt++;
        }
    }

    if (i >= argc) {
        vga_write("rm: missing operand\n", VGA_COLOR_LIGHT_RED);
        return;
    }

    // Если включён interactive, игнорируем force (как в GNU)
    if (interactive) force = 0;

    for (; i < argc; i++) {
        const char *path = argv[i];
        char full[256];
        if (path[0] != '/')
            path_join(full, sizeof(full), shell_cwd, path);
        else
            strcpy(full, path);

        int is_dir = fs_is_directory(full);
        int ret = 0;

        // Запрашиваем подтверждение, если interactive
        if (interactive) {
            vga_write("rm: remove ", VGA_COLOR_LIGHT_CYAN);
            vga_write(is_dir ? "directory '" : "file '", VGA_COLOR_LIGHT_CYAN);
            vga_write(full, VGA_COLOR_LIGHT_CYAN);
            vga_write("'? ", VGA_COLOR_LIGHT_CYAN);
            char answer = getchar();
            vga_write("\n", VGA_COLOR_LIGHT_GREY);
            if (answer != 'y' && answer != 'Y') {
                if (verbose) vga_write("rm: skipping ", VGA_COLOR_LIGHT_BROWN);
                continue;
            }
        }

        if (recursive && is_dir) {
            ret = fs_rm_rf(full);
        } else if (dir_only && is_dir) {
            ret = fs_delete(full); // удаляет только пустую директорию
        } else if (!is_dir) {
            ret = fs_delete(full);
        } else {
            // Попытка удалить директорию без -r или -d
            if (!force) {
                vga_write("rm: cannot remove '", VGA_COLOR_LIGHT_RED);
                vga_write(full, VGA_COLOR_LIGHT_RED);
                vga_write("': Is a directory\n", VGA_COLOR_LIGHT_RED);
            }
            continue;
        }

        if (ret != 0 && !force) {
            vga_write("rm: cannot remove '", VGA_COLOR_LIGHT_RED);
            vga_write(full, VGA_COLOR_LIGHT_RED);
            vga_write("': ", VGA_COLOR_LIGHT_RED);
            if (fs_exists(full))
                vga_write("Operation failed\n", VGA_COLOR_LIGHT_RED);
            else
                vga_write("No such file or directory\n", VGA_COLOR_LIGHT_RED);
        } else if (verbose && ret == 0) {
            vga_write("rm: removed ", VGA_COLOR_LIGHT_GREEN);
            vga_write(full, VGA_COLOR_LIGHT_GREEN);
            vga_write("\n", VGA_COLOR_LIGHT_GREEN);
        }
    }
}

static void do_cat(int argc, char **argv) {
    if (argc < 2) {
        vga_write("cat: need file\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    for (int i = 1; i < argc; i++) {
        char *path = argv[i];
        char full[256];
        if (path[0] != '/')
            path_join(full, sizeof(full), shell_cwd, path);
        else
            strcpy(full, path);
        fs_cat_path(full);
    }
}

static void do_touch(int argc, char **argv) {
    if (argc < 2) {
        vga_write("touch: need file\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    for (int i = 1; i < argc; i++) {
        char *path = argv[i];
        char full[256];
        if (path[0] != '/')
            path_join(full, sizeof(full), shell_cwd, path);
        else
            strcpy(full, path);
        if (fs_exists(full))
            continue;
        if (fs_create(full, 0) != 0)
            vga_write("touch: failed\n", VGA_COLOR_LIGHT_RED);
    }
}
// Проверяет, является ли `child` подкаталогом `parent` (или равен ему)
static int is_subpath(const char *parent, const char *child) {
    size_t plen = strlen(parent);
    if (strncmp(parent, child, plen) != 0) return 0;
    // parent == child или child[plen] == '/'
    return (child[plen] == '/' || child[plen] == '\0');
}

static int copy_file(const char *src, const char *dst, int verbose) {
    static uint8_t buf[12 * 4096];
    uint32_t sz;
    if (fs_open(src, buf, sizeof(buf), &sz) != 0) {
        if (verbose) vga_write("cp: cannot open source\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (sz > sizeof(buf)) {
        if (verbose) vga_write("cp: file too large (only direct blocks)\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    if (fs_exists(dst)) {
        if (fs_delete(dst) != 0) return -1;
    }
    if (fs_create(dst, 0) != 0) return -1;
    if (fs_write(dst, buf, sz) != 0) return -1;
    if (verbose) {
        vga_write("cp: '", VGA_COLOR_LIGHT_GREEN);
        vga_write(src, VGA_COLOR_LIGHT_GREEN);
        vga_write("' -> '", VGA_COLOR_LIGHT_GREEN);
        vga_write(dst, VGA_COLOR_LIGHT_GREEN);
        vga_write("'\n", VGA_COLOR_LIGHT_GREEN);
    }
    return 0;
}

static int copy_directory(const char *src_dir, const char *dst_dir, int verbose, int interactive, int force) {
    // Защита от копирования каталога в самого себя
    if (strcmp(src_dir, dst_dir) == 0) {
        if (verbose) vga_write("cp: cannot copy directory to itself\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    // Проверка, не является ли dst_dir подкаталогом src_dir
    size_t src_len = strlen(src_dir);
    if (strncmp(dst_dir, src_dir, src_len) == 0 && dst_dir[src_len] == '/') {
        if (verbose) vga_write("cp: cannot copy directory into itself\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    // Создаём целевую директорию, если её нет
    if (!fs_exists(dst_dir)) {
        if (fs_mkdir(dst_dir) != 0) {
            vga_write("cp: cannot create directory '", VGA_COLOR_LIGHT_RED);
            vga_write(dst_dir, VGA_COLOR_LIGHT_RED);
            vga_write("'\n", VGA_COLOR_LIGHT_RED);
            return -1;
        }
        if (verbose) {
            vga_write("cp: created directory '", VGA_COLOR_LIGHT_GREEN);
            vga_write(dst_dir, VGA_COLOR_LIGHT_GREEN);
            vga_write("'\n", VGA_COLOR_LIGHT_GREEN);
        }
    } else if (!fs_is_directory(dst_dir)) {
        vga_write("cp: target '", VGA_COLOR_LIGHT_RED);
        vga_write(dst_dir, VGA_COLOR_LIGHT_RED);
        vga_write("' is not a directory\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    // Получаем содержимое исходной директории
    fs_dirent_t entries[256];
    int n = fs_readdir(src_dir, entries, 256);
    if (n < 0) {
        vga_write("cp: cannot read directory '", VGA_COLOR_LIGHT_RED);
        vga_write(src_dir, VGA_COLOR_LIGHT_RED);
        vga_write("'\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    // Обрабатываем каждый элемент
    for (int i = 0; i < n; i++) {
        char src_path[512], dst_path[512];
        path_join(src_path, sizeof(src_path), src_dir, entries[i].name);
        path_join(dst_path, sizeof(dst_path), dst_dir, entries[i].name);
        if (entries[i].type == 2) { // директория
            // Рекурсивный вызов
            if (copy_directory(src_path, dst_path, verbose, interactive, force) != 0) {
                return -1;
            }
        } else { // файл
            if (fs_exists(dst_path) && !force) {
                if (interactive) {
                    vga_write("cp: overwrite '", VGA_COLOR_LIGHT_CYAN);
                    vga_write(dst_path, VGA_COLOR_LIGHT_CYAN);
                    vga_write("'? ", VGA_COLOR_LIGHT_CYAN);
                    char ans = getchar();
                    vga_write("\n", VGA_COLOR_LIGHT_GREY);
                    if (ans != 'y' && ans != 'Y') {
                        if (verbose) vga_write("cp: skipping '", VGA_COLOR_LIGHT_BROWN);
                        continue;
                    }
                } else {
                    if (fs_delete(dst_path) != 0) {
                        vga_write("cp: cannot remove existing '", VGA_COLOR_LIGHT_RED);
                        vga_write(dst_path, VGA_COLOR_LIGHT_RED);
                        vga_write("'\n", VGA_COLOR_LIGHT_RED);
                        continue;
                    }
                }
            }
            if (copy_file(src_path, dst_path, verbose) != 0) {
                vga_write("cp: failed to copy file '", VGA_COLOR_LIGHT_RED);
                vga_write(src_path, VGA_COLOR_LIGHT_RED);
                vga_write("'\n", VGA_COLOR_LIGHT_RED);
                return -1;
            }
        }
    }
    return 0;
}

static void do_cp(int argc, char **argv) {
    int interactive = 0, verbose = 0, force = 0, recursive = 0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *opt = argv[i] + 1;
        if (opt[0] == '-') break;
        while (*opt) {
            switch (*opt) {
                case 'i': interactive = 1; force = 0; break;
                case 'v': verbose = 1; break;
                case 'f': force = 1; interactive = 0; break;
                case 'r': recursive = 1; break;
                default:
                    vga_write("cp: invalid option -- '", VGA_COLOR_LIGHT_RED);
                    vga_putchar(*opt, VGA_COLOR_LIGHT_RED);
                    vga_write("'\n", VGA_COLOR_LIGHT_RED);
                    vga_write("Usage: cp [-i] [-v] [-f] source target\n", VGA_COLOR_LIGHT_RED);
                    vga_write("       cp [-i] [-v] [-r] source... directory\n", VGA_COLOR_LIGHT_RED);
                    return;
            }
            opt++;
        }
    }
    if (argc - i < 2) {
        vga_write("cp: missing file operand\n", VGA_COLOR_LIGHT_RED);
        return;
    }

    char dst_full[256];
    if (argv[argc-1][0] != '/')
        path_join(dst_full, sizeof(dst_full), shell_cwd, argv[argc-1]);
    else
        strcpy(dst_full, argv[argc-1]);

    int dst_is_dir = fs_is_directory(dst_full);

    if (argc - i == 2 && !dst_is_dir) {
        char src_full[256];
        if (argv[i][0] != '/')
            path_join(src_full, sizeof(src_full), shell_cwd, argv[i]);
        else
            strcpy(src_full, argv[i]);

        if (fs_is_directory(src_full)) {
            vga_write("cp: omitting directory '", VGA_COLOR_LIGHT_RED);
            vga_write(src_full, VGA_COLOR_LIGHT_RED);
            vga_write("'\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        if (strcmp(src_full, dst_full) == 0) {
            if (verbose) vga_write("cp: '", VGA_COLOR_LIGHT_BROWN);
            return;
        }
        if (fs_exists(dst_full) && !force) {
            if (interactive) {
                vga_write("cp: overwrite '", VGA_COLOR_LIGHT_CYAN);
                vga_write(dst_full, VGA_COLOR_LIGHT_CYAN);
                vga_write("'? ", VGA_COLOR_LIGHT_CYAN);
                char ans = getchar();
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
                if (ans != 'y' && ans != 'Y') {
                    if (verbose) vga_write("cp: skipping '", VGA_COLOR_LIGHT_BROWN);
                    return;
                }
            } else {
                if (fs_delete(dst_full) != 0) {
                    vga_write("cp: cannot remove existing file '", VGA_COLOR_LIGHT_RED);
                    vga_write(dst_full, VGA_COLOR_LIGHT_RED);
                    vga_write("'\n", VGA_COLOR_LIGHT_RED);
                    return;
                }
            }
        }
        if (copy_file(src_full, dst_full, verbose) != 0)
            vga_write("cp: failed to copy\n", VGA_COLOR_LIGHT_RED);
    } else {
        if (!dst_is_dir) {
            vga_write("cp: target '", VGA_COLOR_LIGHT_RED);
            vga_write(dst_full, VGA_COLOR_LIGHT_RED);
            vga_write("' is not a directory\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        for (int j = i; j < argc-1; j++) {
            char src_full[256];
            if (argv[j][0] != '/')
                path_join(src_full, sizeof(src_full), shell_cwd, argv[j]);
            else
                strcpy(src_full, argv[j]);

            if (fs_is_directory(src_full)) {
                if (!recursive) {
                    vga_write("cp: omitting directory '", VGA_COLOR_LIGHT_RED);
                    vga_write(src_full, VGA_COLOR_LIGHT_RED);
                    vga_write("' (use -r)\n", VGA_COLOR_LIGHT_RED);
                    continue;
                } else {
                    char dest_path[512];
                    path_join(dest_path, sizeof(dest_path), dst_full, base_name(src_full));
                    if (copy_directory(src_full, dest_path, verbose, interactive, force) != 0) {
                        vga_write("cp: failed to copy directory '", VGA_COLOR_LIGHT_RED);
                        vga_write(src_full, VGA_COLOR_LIGHT_RED);
                        vga_write("'\n", VGA_COLOR_LIGHT_RED);
                    }
                    continue;
                }
            }

            char dest_path[512];
            path_join(dest_path, sizeof(dest_path), dst_full, base_name(src_full));
            if (strcmp(src_full, dest_path) == 0) {
                if (verbose) vga_write("cp: skipping identical '", VGA_COLOR_LIGHT_BROWN);
                continue;
            }
            if (fs_exists(dest_path) && !force) {
                if (interactive) {
                    vga_write("cp: overwrite '", VGA_COLOR_LIGHT_CYAN);
                    vga_write(dest_path, VGA_COLOR_LIGHT_CYAN);
                    vga_write("'? ", VGA_COLOR_LIGHT_CYAN);
                    char ans = getchar();
                    vga_write("\n", VGA_COLOR_LIGHT_GREY);
                    if (ans != 'y' && ans != 'Y') {
                        if (verbose) vga_write("cp: skipping '", VGA_COLOR_LIGHT_BROWN);
                        continue;
                    }
                } else {
                    if (fs_delete(dest_path) != 0) {
                        vga_write("cp: cannot remove existing '", VGA_COLOR_LIGHT_RED);
                        vga_write(dest_path, VGA_COLOR_LIGHT_RED);
                        vga_write("'\n", VGA_COLOR_LIGHT_RED);
                        continue;
                    }
                }
            }
            if (copy_file(src_full, dest_path, verbose) != 0) {
                vga_write("cp: failed to copy '", VGA_COLOR_LIGHT_RED);
                vga_write(src_full, VGA_COLOR_LIGHT_RED);
                vga_write("'\n", VGA_COLOR_LIGHT_RED);
            }
        }
    }
    vga_taskbar_refresh();
}

static void do_mv(int argc, char **argv) {
    if (argc < 3) {
        vga_write("mv: missing file operand\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    int i = 1;
    int interactive = 0, verbose = 0, force = 0;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *opt = argv[i] + 1;
        if (opt[0] == '-') break;
        while (*opt) {
            switch (*opt) {
                case 'i': interactive = 1; force = 0; break;
                case 'v': verbose = 1; break;
                case 'f': force = 1; interactive = 0; break;
                default:
                    vga_write("mv: invalid option -- '", VGA_COLOR_LIGHT_RED);
                    vga_putchar(*opt, VGA_COLOR_LIGHT_RED);
                    vga_write("'\n", VGA_COLOR_LIGHT_RED);
                    return;
            }
            opt++;
        }
    }
    if (argc - i < 2) {
        vga_write("mv: missing destination\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    char dst_full[256];
    if (argv[argc-1][0] != '/')
        path_join(dst_full, sizeof(dst_full), shell_cwd, argv[argc-1]);
    else
        strcpy(dst_full, argv[argc-1]);
    int dst_is_dir = fs_is_directory(dst_full);
    if (argc - i == 2) {
        char src_full[256];
        if (argv[i][0] != '/')
            path_join(src_full, sizeof(src_full), shell_cwd, argv[i]);
        else
            strcpy(src_full, argv[i]);
        char target[512];
        if (dst_is_dir) {
            path_join(target, sizeof(target), dst_full, base_name(src_full));
        } else {
            strcpy(target, dst_full);
        }
        if (strcmp(src_full, target) == 0) {
            if (verbose) vga_write("mv: cannot move to itself\n", VGA_COLOR_LIGHT_BROWN);
            return;
        }
        if (fs_exists(target) && !force) {
            if (interactive) {
                vga_write("mv: overwrite '", VGA_COLOR_LIGHT_CYAN);
                vga_write(target, VGA_COLOR_LIGHT_CYAN);
                vga_write("'? ", VGA_COLOR_LIGHT_CYAN);
                char ans = getchar();
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
                if (ans != 'y' && ans != 'Y') {
                    if (verbose) vga_write("mv: skipping\n", VGA_COLOR_LIGHT_BROWN);
                    return;
                }
            } else {
                if (fs_delete(target) != 0) {
                    vga_write("mv: cannot remove existing '", VGA_COLOR_LIGHT_RED);
                    vga_write(target, VGA_COLOR_LIGHT_RED);
                    vga_write("'\n", VGA_COLOR_LIGHT_RED);
                    return;
                }
            }
        }
        if (fs_rename(src_full, target) != 0) {
            vga_write("mv: failed\n", VGA_COLOR_LIGHT_RED);
        } else if (verbose) {
            vga_write("mv: '", VGA_COLOR_LIGHT_GREEN);
            vga_write(src_full, VGA_COLOR_LIGHT_GREEN);
            vga_write("' -> '", VGA_COLOR_LIGHT_GREEN);
            vga_write(target, VGA_COLOR_LIGHT_GREEN);
            vga_write("'\n", VGA_COLOR_LIGHT_GREEN);
        }
    } else {
        if (!dst_is_dir) {
            vga_write("mv: target '", VGA_COLOR_LIGHT_RED);
            vga_write(dst_full, VGA_COLOR_LIGHT_RED);
            vga_write("' is not a directory\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        for (int j = i; j < argc-1; j++) {
            char src_full[256];
            if (argv[j][0] != '/')
                path_join(src_full, sizeof(src_full), shell_cwd, argv[j]);
            else
                strcpy(src_full, argv[j]);
            char target[512];
            path_join(target, sizeof(target), dst_full, base_name(src_full));
            if (strcmp(src_full, target) == 0) continue;
            if (fs_exists(target) && !force) {
                if (interactive) {
                    vga_write("mv: overwrite '", VGA_COLOR_LIGHT_CYAN);
                    vga_write(target, VGA_COLOR_LIGHT_CYAN);
                    vga_write("'? ", VGA_COLOR_LIGHT_CYAN);
                    char ans = getchar();
                    vga_write("\n", VGA_COLOR_LIGHT_GREY);
                    if (ans != 'y' && ans != 'Y') {
                        if (verbose) vga_write("mv: skipping\n", VGA_COLOR_LIGHT_BROWN);
                        continue;
                    }
                } else {
                    if (fs_delete(target) != 0) continue;
                }
            }
            if (fs_rename(src_full, target) != 0) {
                vga_write("mv: failed to move '", VGA_COLOR_LIGHT_RED);
                vga_write(src_full, VGA_COLOR_LIGHT_RED);
                vga_write("'\n", VGA_COLOR_LIGHT_RED);
            } else if (verbose) {
                vga_write("mv: '", VGA_COLOR_LIGHT_GREEN);
                vga_write(src_full, VGA_COLOR_LIGHT_GREEN);
                vga_write("' -> '", VGA_COLOR_LIGHT_GREEN);
                vga_write(target, VGA_COLOR_LIGHT_GREEN);
                vga_write("'\n", VGA_COLOR_LIGHT_GREEN);
            }
        }
    }
    vga_taskbar_refresh();
}

static void do_sync(void) {
    fs_sync_to_disk();
    ata_flush();
    vga_write("sync: done\n", VGA_COLOR_LIGHT_GREEN);
}

static void do_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_write(" ", VGA_COLOR_LIGHT_GREY);
        vga_write(argv[i], VGA_COLOR_LIGHT_GREY);
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void do_uname(int argc, char **argv) {
    int all = (argc >= 2 && strcmp(argv[1], "-a") == 0);
    if (all)
        vga_write("YodaOS yoda 0.0 #1 x86_64 YodaOS\n", VGA_COLOR_LIGHT_GREY);
    else
        vga_write("YodaOS\n", VGA_COLOR_LIGHT_GREY);
}

static void do_wc(int argc, char **argv) {
    if (argc < 2) {
        vga_write("wc: need file\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    char full[256];
    if (argv[1][0] != '/')
        path_join(full, sizeof(full), shell_cwd, argv[1]);
    else
        strcpy(full, argv[1]);
    uint8_t buf[8192];
    uint32_t sz;
    if (fs_open(full, buf, sizeof(buf), &sz) != 0) {
        vga_write("wc: read failed\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    int lc = 0, wc = 0, inw = 0;
    for (uint32_t i = 0; i < sz; i++) {
        char c = buf[i];
        if (c == '\n') lc++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inw) { wc++; inw = 0; }
        } else inw = 1;
    }
    if (inw) wc++;
    char tmp[16];
    int_to_str(lc, tmp); vga_write(tmp, VGA_COLOR_LIGHT_GREY); vga_write(" ", VGA_COLOR_LIGHT_GREY);
    int_to_str(wc, tmp); vga_write(tmp, VGA_COLOR_LIGHT_GREY); vga_write(" ", VGA_COLOR_LIGHT_GREY);
    int_to_str((int)sz, tmp); vga_write(tmp, VGA_COLOR_LIGHT_GREY); vga_write(" ", VGA_COLOR_LIGHT_GREY);
    vga_write(full, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void do_head(int argc, char **argv) {
    if (argc < 2) {
        vga_write("head: need file\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    char full[256];
    if (argv[1][0] != '/')
        path_join(full, sizeof(full), shell_cwd, argv[1]);
    else
        strcpy(full, argv[1]);
    uint8_t buf[8192];
    uint32_t sz;
    if (fs_open(full, buf, sizeof(buf), &sz) != 0) {
        vga_write("head: read failed\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    int lines = 0;
    for (uint32_t i = 0; i < sz && lines < 10; i++) {
        vga_putchar(buf[i], VGA_COLOR_LIGHT_GREY);
        if (buf[i] == '\n') lines++;
    }
}

static void do_tail(int argc, char **argv) {
    if (argc < 2) {
        vga_write("tail: need file\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    char full[256];
    if (argv[1][0] != '/')
        path_join(full, sizeof(full), shell_cwd, argv[1]);
    else
        strcpy(full, argv[1]);
    uint8_t buf[8192];
    uint32_t sz;
    if (fs_open(full, buf, sizeof(buf), &sz) != 0) {
        vga_write("tail: read failed\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    int lines = 0;
    for (uint32_t i = 0; i < sz; i++)
        if (buf[i] == '\n') lines++;
    int start = 0, count = 0;
    for (uint32_t i = 0; i < sz; i++) {
        if (buf[i] == '\n') count++;
        if (count >= lines - 10) {
            start = i + 1;
            break;
        }
    }
    for (uint32_t i = start; i < sz; i++)
        vga_putchar(buf[i], VGA_COLOR_LIGHT_GREY);
}

static void do_grep(int argc, char **argv) {
    if (argc < 3) {
        vga_write("Usage: grep <pattern> <file>\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    const char *pat = argv[1];
    char full[256];
    if (argv[2][0] != '/')
        path_join(full, sizeof(full), shell_cwd, argv[2]);
    else
        strcpy(full, argv[2]);
    uint8_t buf[8192];
    uint32_t sz;
    if (fs_open(full, buf, sizeof(buf), &sz) != 0) {
        vga_write("grep: read failed\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    size_t plen = strlen(pat);
    int line_start = 0;
    for (uint32_t i = 0; i <= sz; i++) {
        if (i == sz || buf[i] == '\n') {
            int hit = 0;
            int linlen = i - line_start;
            if (plen == 0) hit = 1;
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
                    vga_putchar(buf[k], VGA_COLOR_LIGHT_GREY);
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
            }
            line_start = i + 1;
        }
    }
}
static void do_exec(int argc, char **argv) {
    if (argc < 2) {
        vga_write("Usage: exec <file>\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    char full[256];
    if (argv[1][0] == '/')
        strcpy(full, argv[1]);
    else
        path_join(full, sizeof(full), shell_cwd, argv[1]);

    // Принудительно сбрасываем текущий каталог в корень, чтобы избежать проблем
    change_dir("/");

    vga_write("do_exec: trying to open ", VGA_COLOR_LIGHT_CYAN);
    vga_write(full, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_CYAN);

    uint64_t entry, stack_top;
    if (load_elf(full, &entry, &stack_top) != 0) {
        vga_write("exec: failed to load ELF\n", VGA_COLOR_LIGHT_RED);
        return;
    }

    vga_write("exec: jumping to ring3...\n", VGA_COLOR_LIGHT_CYAN);
    jump_user_ring3(entry, stack_top);
    vga_write("exec: returned from ring3\n", VGA_COLOR_LIGHT_GREEN);
}
void do_shlog(){
    cmd_ynan_at("/shlog.log");
}
// ------------------------------------------------------------
// Диспетчер команд
// ------------------------------------------------------------
void shell_dispatch_line(const char *cmd) {
    if (!cmd || cmd[0] == '\0') {
        vga_taskbar_refresh();
        return;
    }

    char line[256];
    strcpy(line, cmd);
    char *argv[16];
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) return;

    const char *a0 = argv[0];

    // Встроенные команды ядра (не файловые)
    if (strcmp(a0, "reboot") == 0) { cmd_reboot(); return; }
    if (strcmp(a0, "shutdown") == 0) { cmd_shutdown(); return; }
    if (strcmp(a0, "sysinf") == 0) { cmd_sysinf(); return; }
    if (strcmp(a0, "clear") == 0) { cmd_clear(); return; }
    if (strcmp(a0, "asciiart") == 0) { cmd_asciiart(); return; }
    if (strcmp(a0, "cpuid") == 0) { cmd_cpuid(); return; }
    if (strcmp(a0, "memtest") == 0) { cmd_memtest(); return; }
    if (strcmp(a0, "rand") == 0) { cmd_rand(); return; }
    if (strcmp(a0, "pause") == 0) { cmd_pause(); return; }
    if (strcmp(a0, "format") == 0) { cmd_format(); return; }
    if (strcmp(a0, "mkfs") == 0) { cmd_mkfs(); return; }
    if (strcmp(a0, "shlog") == 0) {do_shlog(); return; }
    if (strcmp(a0, "date") == 0) {
        int d = 0, t = 0;
        for (int i = 1; i < argc; i++) {
            if (strstr(argv[i], "-d")) d = 1;
            if (strstr(argv[i], "-t")) t = 1;
        }
        if (!d && !t) d = t = 1;
        cmd_date(d, t);
        return;
    }
    if (strcmp(a0, "help") == 0) {
        int page = 1;
        if (argc >= 2 && strncmp(argv[1], "-p", 2) == 0 && argc >= 3)
            page = shell_atoi(argv[2]);
        cmd_help_p(page);
        return;
    }
    if (strcmp(a0, "ynan") == 0) {
        if (argc < 2) vga_write("Usage: ynan <file>\n", VGA_COLOR_LIGHT_RED);
        else {
            char full[256];
            if (argv[1][0] == '/') strcpy(full, argv[1]);
            else path_join(full, sizeof(full), shell_cwd, argv[1]);
            cmd_ynan_at(full);
        }
        return;
    }
    if (strcmp(a0, "ring3test") == 0) {
        vga_write("Entering ring3...\n", VGA_COLOR_LIGHT_CYAN);
        jump_user_ring3((uint64_t)(uintptr_t)ring3_demo_entry, 0x600000ull);
        return;
    }
    if (strcmp(a0, "panic_test") == 0) {
        panic_handler(14);
        return;
    }
    if (strcmp(a0, "exec") == 0) do_exec(argc, argv);
    // Команды файловой системы (используем наши do_*)
    if (strcmp(a0, "ls") == 0) do_ls(argc, argv);
    else if (strcmp(a0, "cd") == 0) do_cd(argc, argv);
    else if (strcmp(a0, "pwd") == 0) do_pwd();
    else if (strcmp(a0, "mkdir") == 0) do_mkdir(argc, argv);
    else if (strcmp(a0, "rmdir") == 0) do_rmdir(argc, argv);
    else if (strcmp(a0, "rm") == 0) do_rm(argc, argv);
    else if (strcmp(a0, "cat") == 0) do_cat(argc, argv);
    else if (strcmp(a0, "touch") == 0) do_touch(argc, argv);
    else if (strcmp(a0, "cp") == 0) do_cp(argc, argv);
    else if (strcmp(a0, "mv") == 0) do_mv(argc, argv);
    else if (strcmp(a0, "sync") == 0) do_sync();
    else if (strcmp(a0, "echo") == 0) do_echo(argc, argv);
    else if (strcmp(a0, "uname") == 0) do_uname(argc, argv); 
    else if (strcmp(a0, "wc") == 0) do_wc(argc, argv);
    else if (strcmp(a0, "head") == 0) do_head(argc, argv);
    else if (strcmp(a0, "tail") == 0) do_tail(argc, argv);
    else if (strcmp(a0, "grep") == 0) do_grep(argc, argv);
    else {
        vga_write("Unknown command\n", VGA_COLOR_LIGHT_RED);
    }

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
        change_dir("/");
        shell_update_cwd();
        vga_write("(ext2 mounted, cwd /)\n", VGA_COLOR_LIGHT_CYAN);
    } else {
        vga_write("(ext2 not mounted, use format)\n", VGA_COLOR_LIGHT_RED);
    }
    vga_write("\n$> ", VGA_COLOR_LIGHT_GREEN);
    while (1) {
        uint8_t sc = wait_for_key();
        if (sc == 0xE0) {
            uint8_t sc2 = wait_for_key();
            if (sc2 == 0x48) { // up
                if (history_count > 0 && history_index < history_count - 1) {
                    history_index++;
                    while (cmd_pos > 0) { cmd_pos--; vga_putchar('\b', VGA_COLOR_LIGHT_GREY); }
                    strcpy(cmd, history[history_count - 1 - history_index]);
                    cmd_pos = strlen(cmd);
                    vga_write(cmd, VGA_COLOR_LIGHT_GREY);
                }
            } else if (sc2 == 0x50) { // down
                if (history_index > 0) {
                    history_index--;
                    while (cmd_pos > 0) { cmd_pos--; vga_putchar('\b', VGA_COLOR_LIGHT_GREY); }
                    strcpy(cmd, history[history_count - 1 - history_index]);
                    cmd_pos = strlen(cmd);
                    vga_write(cmd, VGA_COLOR_LIGHT_GREY);
                } else if (history_index == 0) {
                    history_index = -1;
                    while (cmd_pos > 0) { cmd_pos--; vga_putchar('\b', VGA_COLOR_LIGHT_GREY); }
                    cmd[0] = 0;
                    cmd_pos = 0;
                }
            }
            continue;
        }
        char c = scancode_to_char(sc);
        if (c == '\n' || c == '\r') {
            cmd[cmd_pos] = 0;
            vga_write("\n", VGA_COLOR_LIGHT_GREY);
            if (cmd[0] != 0) {
                if (history_count == 0 || strcmp(history[history_count - 1], cmd) != 0) {
                    if (history_count < HISTORY_SIZE)
                        strcpy(history[history_count++], cmd);
                    else {
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
            if (cmd_pos > 0) { cmd_pos--; vga_putchar('\b', VGA_COLOR_LIGHT_GREY); }
        } else if (c >= ' ' && c <= '~' && cmd_pos < 63) {
            cmd[cmd_pos++] = c;
            vga_putchar(c, VGA_COLOR_LIGHT_GREY);
        }
    }
}