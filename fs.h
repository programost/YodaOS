#ifndef FS_H
#define FS_H

#include "types.h"

#define MAX_FILENAME 32
#define MAX_FILES    64
#define ROOT_DIR     "/"

#define YFS_MAGIC    0x59465320
#define YFS_VERSION  1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint8_t  reserved[492];
} __attribute__((packed)) yfs_superblock_t;

typedef struct {
    char name[MAX_FILENAME];
    uint32_t size;
    uint32_t first_block;
    uint8_t flags;
    int32_t parent;
} file_entry_t;

extern uint32_t partition_offset;
extern int file_count;

void fs_init(void);
int fs_create(const char *name, uint8_t is_dir);
int fs_delete(const char *name);
int fs_remove(const char *name);
int fs_open(const char *name, uint8_t *data, uint32_t *size);
int fs_write(const char *name, const uint8_t *data, uint32_t size);
void fs_list(void);
int fs_change_dir(const char *path);
void fs_get_cwd(char *buf, int len);
int fs_mkdir(const char *name);
int fs_cd(const char *path);
int fs_is_directory(const char *name);
void fs_sync_to_disk(void);
void fs_load_from_disk(void);
int fs_exists(const char *path);

void cmd_ls(void);
void cmd_cat(const char *fname);
void cmd_touch(const char *fname);
void cmd_pwd(void);
void cmd_ynan(const char *fname);
void cmd_rm(const char *arg);

#endif
