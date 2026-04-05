#ifndef FS_H
#define FS_H

#include <stdint.h>

#define MAX_FILENAME 32
#define MAX_FILES    64
#define ROOT_DIR     "/"

typedef struct {
    char name[MAX_FILENAME];
    uint32_t size;
    uint32_t first_block;
    uint8_t flags;          
    int32_t parent;
} file_entry_t;

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

void cmd_ls(void);
void cmd_cat(const char *fname);
void cmd_touch(const char *fname);
void cmd_pwd(void);
void cmd_ynan(const char *fname);
void cmd_rm(const char *arg);      

#endif