#include "fs.h"
#include "drivers.h"
#include "kernel.h"
#include "string.h"
#include <stddef.h>

static file_entry_t file_table[MAX_FILES];
static int file_count = 0;
static int cwd_id = 0;
static uint8_t disk_blocks[1024][512];

#define SUPERBLOCK_LBA   1
#define TABLE_START_LBA  2
#define DATA_START_LBA   8

static int find_file_by_fullpath(const char *abs_path) {
    for (int i = 0; i < file_count; i++)
        if (strcmp(file_table[i].name, abs_path) == 0)
            return i;
    return -1;
}

static void normalize_path(char *path) {
    for (char *p = path; *p; p++) {
        if (*p == '/' && *(p+1) == '/') {
            char *q = p+1;
            while (*q) {
                *q = *(q+1);
                q++;
            }
            p--;
        }
    }
}

static int resolve_path(const char *path, char *abs_path, int abs_len) {
    if (path[0] == '/') {
        strncpy(abs_path, path, abs_len - 1);
        abs_path[abs_len - 1] = 0;
    } else {
        char cwd_buf[64];
        fs_get_cwd(cwd_buf, sizeof(cwd_buf));
        if (strcmp(cwd_buf, "/") == 0) {
            abs_path[0] = '/';
            strcpy(abs_path + 1, path);
        } else {
            strcpy(abs_path, cwd_buf);
            strcat(abs_path, "/");
            strcat(abs_path, path);
        }
    }
    normalize_path(abs_path);
    return find_file_by_fullpath(abs_path);
}

void fs_sync_to_disk(void) {
    if (ata_init() != 0) return;
    uint8_t table_buf[512 * 6];
    memset(table_buf, 0, sizeof(table_buf));
    memcpy(table_buf, file_table, sizeof(file_table));
    for (int i = 0; i < 6; i++)
        ata_write_sectors(TABLE_START_LBA + i, 1, table_buf + i * 512);
    for (int i = 0; i < 1024; i++)
        ata_write_sectors(DATA_START_LBA + i, 1, disk_blocks[i]);
}

void fs_load_from_disk(void) {
    if (ata_init() != 0) return;
    uint8_t table_buf[512 * 6];
    for (int i = 0; i < 6; i++)
        ata_read_sectors(TABLE_START_LBA + i, 1, table_buf + i * 512);
    memcpy(file_table, table_buf, sizeof(file_table));
    if (strcmp(file_table[0].name, ROOT_DIR) != 0 || file_table[0].parent != -1) {
        fs_init();
        return;
    }
    file_count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].name[0] == 0) break;
        file_count++;
    }
    for (int i = 0; i < 1024; i++)
        ata_read_sectors(DATA_START_LBA + i, 1, disk_blocks[i]);
    cwd_id = 0;
}

void fs_init(void) {
    memset(file_table, 0, sizeof(file_table));
    memset(disk_blocks, 0, sizeof(disk_blocks));
    strcpy(file_table[0].name, ROOT_DIR);
    file_table[0].flags = 1;
    file_table[0].parent = -1;
    file_count = 1;
    cwd_id = 0;
    fs_load_from_disk();
}

int fs_create(const char *name, uint8_t is_dir) {
    char abs_path[128];
    if (name[0] == '/') {
        strncpy(abs_path, name, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = 0;
    } else {
        char cwd_buf[64];
        fs_get_cwd(cwd_buf, sizeof(cwd_buf));
        if (strcmp(cwd_buf, "/") == 0) {
            abs_path[0] = '/';
            strcpy(abs_path + 1, name);
        } else {
            strcpy(abs_path, cwd_buf);
            strcat(abs_path, "/");
            strcat(abs_path, name);
        }
    }
    normalize_path(abs_path);
    if (find_file_by_fullpath(abs_path) >= 0) return -1;
    if (file_count >= MAX_FILES) return -1;

    strncpy(file_table[file_count].name, abs_path, MAX_FILENAME - 1);
    file_table[file_count].name[MAX_FILENAME - 1] = 0;
    file_table[file_count].size = 0;
    file_table[file_count].first_block = file_count;
    file_table[file_count].flags = is_dir ? 1 : 0;
    file_table[file_count].parent = cwd_id;
    file_count++;
    fs_sync_to_disk();
    return 0;
}

int fs_delete(const char *name) {
    char abs_path[128];
    int idx = resolve_path(name, abs_path, sizeof(abs_path));
    if (idx < 0) return -1;
    for (int i = idx; i < file_count - 1; i++)
        file_table[i] = file_table[i + 1];
    file_count--;
    memset(&file_table[file_count], 0, sizeof(file_entry_t));
    fs_sync_to_disk();
    return 0;
}

int fs_remove(const char *name) {
    return fs_delete(name);
}

int fs_write(const char *name, const uint8_t *data, uint32_t size) {
    char abs_path[128];
    int idx = resolve_path(name, abs_path, sizeof(abs_path));
    if (idx < 0) return -1;

    uint32_t blocks_needed = (size + 511) / 512;
    if (blocks_needed > 1024) blocks_needed = 1024;
    uint32_t block_idx = file_table[idx].first_block;
    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint32_t offset = b * 512;
        uint32_t copy = (size - offset > 512) ? 512 : (size - offset);
        memcpy(disk_blocks[block_idx + b], data + offset, copy);
        if (copy < 512)
            memset(disk_blocks[block_idx + b] + copy, 0, 512 - copy);
    }
    file_table[idx].size = size;
    fs_sync_to_disk();
    return 0;
}

int fs_open(const char *name, uint8_t *data, uint32_t *size) {
    char abs_path[128];
    int idx = resolve_path(name, abs_path, sizeof(abs_path));
    if (idx < 0) return -1;
    *size = file_table[idx].size;
    uint32_t blocks_needed = (*size + 511) / 512;
    uint32_t block_idx = file_table[idx].first_block;
    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint32_t offset = b * 512;
        uint32_t copy = (*size - offset > 512) ? 512 : (*size - offset);
        memcpy(data + offset, disk_blocks[block_idx + b], copy);
    }
    return 0;
}

void fs_list(void) {
    for (int i = 0; i < file_count; i++) {
        if (file_table[i].parent == cwd_id) {
            const char *name = file_table[i].name;
            const char *last = name;
            for (const char *p = name; *p; p++) if (*p == '/') last = p + 1;
            vga_write(last, VGA_COLOR_LIGHT_GREY);
            if (file_table[i].flags) vga_write("/", VGA_COLOR_LIGHT_CYAN);
            vga_write("  ", VGA_COLOR_LIGHT_GREY);
        }
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

int fs_change_dir(const char *path) { return fs_cd(path); }

void fs_get_cwd(char *buf, int len) {
    if (cwd_id == 0) {
        strncpy(buf, ROOT_DIR, len - 1);
        buf[len - 1] = 0;
        return;
    }
    strncpy(buf, file_table[cwd_id].name, len - 1);
    buf[len - 1] = 0;
}

int fs_mkdir(const char *name) { return fs_create(name, 1); }

int fs_cd(const char *path) {
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        cwd_id = 0;
        return 0;
    }
    char abs_path[128];
    if (path[0] == '/') {
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = 0;
    } else {
        char cwd_buf[64];
        fs_get_cwd(cwd_buf, sizeof(cwd_buf));
        if (strcmp(cwd_buf, "/") == 0) {
            abs_path[0] = '/';
            strcpy(abs_path + 1, path);
        } else {
            strcpy(abs_path, cwd_buf);
            strcat(abs_path, "/");
            strcat(abs_path, path);
        }
    }
    normalize_path(abs_path);
    for (int i = 0; i < file_count; i++) {
        if ((file_table[i].flags & 1) && strcmp(file_table[i].name, abs_path) == 0) {
            cwd_id = i;
            return 0;
        }
    }
    return -1;
}

int fs_is_directory(const char *name) {
    char abs_path[128];
    int idx = resolve_path(name, abs_path, sizeof(abs_path));
    if (idx < 0) return 0;
    return (file_table[idx].flags & 1) ? 1 : 0;
}

void cmd_rm(const char *arg) {
    while (*arg == ' ') arg++;
    if (strncmp(arg, "-d ", 3) == 0) {
        const char *dirname = arg + 3;
        while (*dirname == ' ') dirname++;
        if (!fs_is_directory(dirname)) {
            vga_write("Not a directory or doesn't exist\n", VGA_COLOR_LIGHT_RED);
        } else {
            if (fs_delete(dirname) == 0)
                vga_write("Directory removed\n", VGA_COLOR_LIGHT_GREEN);
            else
                vga_write("Failed to remove directory\n", VGA_COLOR_LIGHT_RED);
        }
    } else if (strncmp(arg, "-f ", 3) == 0) {
        const char *filename = arg + 3;
        while (*filename == ' ') filename++;
        if (fs_is_directory(filename)) {
            vga_write("Is a directory (use -d)\n", VGA_COLOR_LIGHT_RED);
        } else {
            if (fs_delete(filename) == 0)
                vga_write("File removed\n", VGA_COLOR_LIGHT_GREEN);
            else
                vga_write("File not found\n", VGA_COLOR_LIGHT_RED);
        }
    } else {
        vga_write("Usage: rm -d <dir> | rm -f <file>\n", VGA_COLOR_LIGHT_RED);
    }
}
void cmd_ls(void) { fs_list(); }
void cmd_cat(const char *fname) {
    uint8_t buf[512];
    uint32_t size;
    if (fs_open(fname, buf, &size) == 0) {
        buf[size] = 0;
        vga_write((char *)buf, VGA_COLOR_LIGHT_GREY);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
    } else {
        vga_write("File not found\n", VGA_COLOR_LIGHT_RED);
    }
}
void cmd_touch(const char *fname) {
    if (fs_create(fname, 0) == 0)
        vga_write("File created\n", VGA_COLOR_LIGHT_GREEN);
    else
        vga_write("Failed to create file\n", VGA_COLOR_LIGHT_RED);
}
void cmd_pwd(void) {
    char buf[64];
    fs_get_cwd(buf, 64);
    vga_write(buf, VGA_COLOR_LIGHT_GREY);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void editor_draw(const char *buffer, int cursor_pos, int start_row, int *out_cursor_x, int *out_cursor_y) {
    int row = start_row;
    int col = 0;
    int current_pos = 0;
    int found_cursor = 0;
    for (int r = start_row; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_MEMORY[r * VGA_WIDTH + c] = (VGA_COLOR_LIGHT_GREY << 8) | ' ';
    for (int i = 0; buffer[i] && row < VGA_HEIGHT; i++) {
        if (buffer[i] == '\n') {
            row++; col = 0; current_pos++;
            continue;
        }
        if (row < VGA_HEIGHT && col < VGA_WIDTH) {
            VGA_MEMORY[row * VGA_WIDTH + col] = (VGA_COLOR_LIGHT_GREY << 8) | buffer[i];
            if (current_pos == cursor_pos) {
                *out_cursor_x = col;
                *out_cursor_y = row;
                found_cursor = 1;
            }
            col++;
        }
        current_pos++;
    }
    if (!found_cursor) {
        int r = start_row, c = 0, pos = 0;
        for (int i = 0; buffer[i] && pos <= cursor_pos; i++) {
            if (buffer[i] == '\n') { r++; c = 0; }
            else if (pos == cursor_pos) break;
            else c++;
            pos++;
        }
        if (r >= VGA_HEIGHT) r = VGA_HEIGHT - 1;
        if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
        *out_cursor_x = c;
        *out_cursor_y = r;
    }
}

void cmd_ynan(const char *fname) {
    uint16_t saved_screen[VGA_WIDTH * VGA_HEIGHT];
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        saved_screen[i] = VGA_MEMORY[i];
    uint8_t saved_cx, saved_cy;
    vga_get_cursor(&saved_cx, &saved_cy);

    vga_clear(VGA_COLOR_BLACK | (VGA_COLOR_BLACK << 4));
    vga_set_cursor(0, 0);
    vga_write("Editing: ", VGA_COLOR_LIGHT_GREY);
    vga_write(fname, VGA_COLOR_LIGHT_BLUE);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    vga_write("Arrow keys to move, ESC to save, '.' + Enter on empty line to save\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("------------------------------------------------------------\n", VGA_COLOR_LIGHT_GREY);

#define MAX_EDIT_SIZE 8192
    char *buffer = (char *)0x200000;
    int size = 0, cursor = 0;
    uint32_t loaded_size = 0;
    if (fs_open(fname, (uint8_t *)buffer, &loaded_size) == 0) {
        size = loaded_size;
        if (size > MAX_EDIT_SIZE - 1) size = MAX_EDIT_SIZE - 1;
        buffer[size] = '\0';
        cursor = size;
    } else {
        buffer[0] = '\0';
        size = 0;
        cursor = 0;
    }

    int cursor_x = 0, cursor_y = 2;
    editor_draw(buffer, cursor, 2, &cursor_x, &cursor_y);
    vga_set_cursor(cursor_x, cursor_y);

    int running = 1;
    while (running) {
        uint8_t sc = wait_for_key();
        if (sc == 0xE0) {
            uint8_t sc2 = wait_for_key();
            switch (sc2) {
                case 0x4B: if (cursor > 0) cursor--; break;
                case 0x4D: if (cursor < size) cursor++; break;
                case 0x48: {
                    int line_start = cursor;
                    while (line_start > 0 && buffer[line_start - 1] != '\n') line_start--;
                    if (line_start == 0) cursor = 0;
                    else {
                        int prev_line_end = line_start - 1;
                        int prev_line_start = prev_line_end;
                        while (prev_line_start > 0 && buffer[prev_line_start - 1] != '\n') prev_line_start--;
                        int prev_line_len = prev_line_end - prev_line_start;
                        int offset = cursor - line_start;
                        if (offset > prev_line_len) offset = prev_line_len;
                        cursor = prev_line_start + offset;
                    }
                } break;
                case 0x50: {
                    int line_start = cursor;
                    while (line_start > 0 && buffer[line_start - 1] != '\n') line_start--;
                    int line_end = cursor;
                    while (line_end < size && buffer[line_end] != '\n') line_end++;
                    if (line_end == size) cursor = size;
                    else {
                        int next_line_start = line_end + 1;
                        int next_line_end = next_line_start;
                        while (next_line_end < size && buffer[next_line_end] != '\n') next_line_end++;
                        int offset = cursor - line_start;
                        int next_line_len = next_line_end - next_line_start;
                        if (offset > next_line_len) offset = next_line_len;
                        cursor = next_line_start + offset;
                    }
                } break;
                default: break;
            }
            editor_draw(buffer, cursor, 2, &cursor_x, &cursor_y);
            vga_set_cursor(cursor_x, cursor_y);
            continue;
        }

        char c = scancode_to_char(sc);
        if (c == 0x1B) { running = 0; break; }
        if (c == '\n') {
            int line_start = cursor;
            while (line_start > 0 && buffer[line_start - 1] != '\n') line_start--;
            if (cursor == line_start && (cursor == 0 || buffer[cursor - 1] == '\n')) {
                if (cursor > 0 && buffer[cursor - 1] == '.' && (cursor - 1 == line_start)) {
                    memmove(buffer + cursor - 1, buffer + cursor, size - cursor);
                    size--; cursor--;
                    buffer[size] = '\0';
                    running = 0;
                    break;
                }
            }
            if (size < MAX_EDIT_SIZE - 1) {
                memmove(buffer + cursor + 1, buffer + cursor, size - cursor);
                buffer[cursor] = '\n';
                size++; cursor++;
                buffer[size] = '\0';
            }
        } else if (c == '\b' && cursor > 0) {
            memmove(buffer + cursor - 1, buffer + cursor, size - cursor);
            size--; cursor--;
            buffer[size] = '\0';
        } else if (c >= ' ' && c <= '~' && size < MAX_EDIT_SIZE - 1) {
            memmove(buffer + cursor + 1, buffer + cursor, size - cursor);
            buffer[cursor] = c;
            size++; cursor++;
            buffer[size] = '\0';
        }
        editor_draw(buffer, cursor, 2, &cursor_x, &cursor_y);
        vga_set_cursor(cursor_x, cursor_y);
    }

    if (running == 0) {
        vga_set_cursor(0, VGA_HEIGHT - 1);
        vga_write("\nSaving...\n", VGA_COLOR_LIGHT_GREEN);
        if (size > 0) {
            if (fs_write(fname, (uint8_t *)buffer, size) == 0)
                vga_write("Saved.\n", VGA_COLOR_LIGHT_GREEN);
            else
                vga_write("Failed to save file.\n", VGA_COLOR_LIGHT_RED);
        } else {
            fs_delete(fname);
            vga_write("Empty file deleted.\n", VGA_COLOR_LIGHT_GREEN);
        }
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = saved_screen[i];
    vga_set_cursor(saved_cx, saved_cy);
}