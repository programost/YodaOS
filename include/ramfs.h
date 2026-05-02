#ifndef RAMFS_H
#define RAMFS_H

#include "types.h"

void ramfs_init(void);
void busybox_seed_ramfs(void);

size_t ramfs_bytes_used(void);
size_t ramfs_bytes_total(void);
int ramfs_usage_percent(void);

void bb_get_cwd(char *buf, size_t len);
int bb_chdir(const char *path);
void bb_list_cwd(void);
void bb_list_abs(const char *bb_abs);
void bb_list_long_cwd(int show_all);
void bb_list_long_abs(const char *bb_abs, int show_all);
int bb_lookup_path(const char *bb_abs);
int bb_cat(const char *path);
int bb_mkdir(const char *path);
int bb_touch(const char *path);
int bb_rm(const char *path, int is_dir);

int bb_path_is_disk_mounted(const char *bb_abs_path);
void bb_disk_to_ext2(const char *bb_abs, char *ext2_out, size_t olen);

void bb_to_abs(const char *cwd, const char *in_path, char *out_abs, size_t olen);

int bb_is_dir(const char *path);
int bb_read_file_abs(const char *bb_abs, uint8_t *buf, size_t cap, size_t *out_len);
int bb_put_file_abs(const char *bb_abs, const uint8_t *data, size_t len);
int bb_mv_abs(const char *from_bb_abs, const char *to_bb_abs);

#endif
