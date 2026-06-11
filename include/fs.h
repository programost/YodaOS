#ifndef FS_H
#define FS_H

#include "types.h"

#define MAX_FILENAME 255
#define EXT2_USE_INDIRECT 0

extern uint32_t partition_offset;


typedef struct {
    char name[256];
    uint32_t ino;
    uint8_t type; // 1 = файл, 2 = директория
} fs_dirent_t;

int fs_readdir(const char *path, fs_dirent_t *entries, int max_entries);
void fs_init(void);
void fs_ensure_mounted(void);
int fs_is_mounted(void);
int fs_create(const char *name, uint8_t is_dir);
int fs_delete(const char *name);
int fs_remove(const char *name);
int fs_open(const char *name, uint8_t *data, uint32_t buf_max, uint32_t *size_out);
int fs_write(const char *name, const uint8_t *data, uint32_t size);
void fs_list(void);
int fs_change_dir(const char *path);
void fs_get_cwd(char *buf, int len);
int fs_mkdir(const char *name);
int fs_cd(const char *path);
int fs_is_directory(const char *name);
void fs_sync_to_disk(void);
int fs_load_from_disk(void);
int fs_exists(const char *path);
int fs_has_user_content(void);
int fs_format_partition(uint32_t part_sectors);
int fs_rm_rf(const char *path);
int fs_rename(const char *oldpath, const char *newpath);


void cmd_ls(void);
void fs_list_at_path(const char *ext2_abs_dir);
void fs_list_long_at_path(const char *ext2_abs_dir, int show_all);
int fs_mv_path(const char *from, const char *to);
void fs_cat_path(const char *ext2_abs_file);
void cmd_ynan_at(const char *ext2_abs_path);
void cmd_cat(const char *fname);
void cmd_touch(const char *fname);
void cmd_pwd(void);
void cmd_ynan(const char *fname);
void cmd_rm(const char *arg);

#endif
