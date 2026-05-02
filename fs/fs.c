#include "fs.h"
#include "drivers.h"
#include "kernel.h"
#include "string.h"
#include "types.h"

uint32_t partition_offset = 0;

#define EXT2_SUPER_MAGIC       0xEF53
#define EXT2_ROOT_INO          2
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_NDIR_BLOCKS       12
#define EXT2_IND_BLOCK         12
#define EXT2_DIND_BLOCK        13
#define EXT2_TIND_BLOCK        14

#define EXT2_S_IFMT  0xF000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2

#define EXT2_NAME_LEN 255

#define MAX_GROUPS 256

static struct ext2_super_block {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_pad;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint8_t  s_reserved[760];
} __attribute__((packed)) sb;

static struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed)) groups[MAX_GROUPS];

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_frag;
    uint8_t  i_fsize;
    uint16_t i_pad1;
    uint16_t i_uid_high;
    uint16_t i_gid_high;
    uint32_t i_reserved2;
} __attribute__((packed));

struct ext2_dir_entry_2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} __attribute__((packed));

static uint8_t io_buf[4096];
static uint8_t inode_disk_buf[4096];
static uint8_t dentry_read_buf[4096];
static uint32_t block_size = 1024;
static uint32_t ptrs_per_block = 256;
static uint32_t num_groups;
static int fs_mounted;
static uint32_t cwd_ino = EXT2_ROOT_INO;
static char cwd_path[256] = "/";

static int alloc_block(void);

static uint32_t now_unix(void) {
    int y, mo, d, h, mi, s;
    get_rtc_date(&y, &mo, &d);
    get_rtc_time(&h, &mi, &s);
    (void)y; (void)mo; (void)d;
    return (uint32_t)(h * 3600 + mi * 60 + s);
}

static uint32_t sect_from_block(uint32_t b) {
    return partition_offset + b * (block_size / 512);
}

static int read_blocks(uint32_t block, uint32_t nblk, void *buf) {
    uint32_t sec = sect_from_block(block);
    uint32_t nsec = nblk * (block_size / 512);
    uint8_t *p = buf;
    while (nsec > 255) {
        if (ata_read_sectors(sec, 255, p) != 0) return -1;
        sec += 255;
        p += 255 * 512;
        nsec -= 255;
    }
    if (nsec && ata_read_sectors(sec, (uint8_t)nsec, p) != 0) return -1;
    return 0;
}

static int write_blocks(uint32_t block, uint32_t nblk, const void *buf) {
    uint32_t sec = sect_from_block(block);
    uint32_t nsec = nblk * (block_size / 512);
    const uint8_t *p = buf;
    while (nsec > 255) {
        if (ata_write_sectors(sec, 255, p) != 0) return -1;
        sec += 255;
        p += 255 * 512;
        nsec -= 255;
    }
    if (nsec && ata_write_sectors(sec, (uint8_t)nsec, p) != 0) return -1;
    return 0;
}

static int read_one_block(uint32_t b, void *buf) {
    return read_blocks(b, 1, buf);
}

static int write_one_block(uint32_t b, const void *buf) {
    return write_blocks(b, 1, buf);
}

static void sync_super(void) {
    sb.s_wtime = now_unix();
    memset(io_buf, 0, block_size);
    memcpy(io_buf, &sb, sizeof(sb));
    write_one_block(1, io_buf);
}

static void sync_group_desc(void) {
    uint32_t total = num_groups * sizeof(groups[0]);
    uint32_t pos = 0;
    while (pos < total) {
        uint32_t blk = 2 + pos / block_size;
        uint32_t off = pos % block_size;
        read_one_block(blk, io_buf);
        uint32_t chunk = block_size - off;
        if (chunk > total - pos) chunk = total - pos;
        memcpy(io_buf + off, (uint8_t *)groups + pos, chunk);
        write_one_block(blk, io_buf);
        pos += chunk;
    }
}

static uint32_t inode_group(uint32_t ino) {
    return (ino - 1) / sb.s_inodes_per_group;
}

static uint32_t inode_index(uint32_t ino) {
    return (ino - 1) % sb.s_inodes_per_group;
}

static int read_inode_raw(uint32_t ino, struct ext2_inode *out) {
    if (ino == 0) return -1;
    uint32_t g = inode_group(ino);
    uint32_t idx = inode_index(ino);
    uint32_t isz = sb.s_inode_size ? sb.s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;
    uint64_t off = (uint64_t)idx * isz;
    uint32_t blk = groups[g].bg_inode_table + (uint32_t)(off / block_size);
    uint32_t o = (uint32_t)(off % block_size);
    if (read_one_block(blk, inode_disk_buf) != 0) return -1;
    if (o + isz > block_size) {
        memcpy(out, inode_disk_buf + o, block_size - o);
        if (read_one_block(blk + 1, inode_disk_buf) != 0) return -1;
        memcpy((uint8_t *)out + (block_size - o), inode_disk_buf, o + isz - block_size);
    } else {
        memcpy(out, inode_disk_buf + o, isz);
    }
    return 0;
}

static int write_inode_raw(uint32_t ino, const struct ext2_inode *in) {
    uint32_t g = inode_group(ino);
    uint32_t idx = inode_index(ino);
    uint32_t isz = sb.s_inode_size ? sb.s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;
    uint64_t off = (uint64_t)idx * isz;
    uint32_t blk = groups[g].bg_inode_table + (uint32_t)(off / block_size);
    uint32_t o = (uint32_t)(off % block_size);
    if (o + isz <= block_size) {
        if (read_one_block(blk, inode_disk_buf) != 0) return -1;
        memcpy(inode_disk_buf + o, in, isz);
        return write_one_block(blk, inode_disk_buf);
    }
    if (read_one_block(blk, inode_disk_buf) != 0) return -1;
    memcpy(inode_disk_buf + o, in, block_size - o);
    if (write_one_block(blk, inode_disk_buf) != 0) return -1;
    if (read_one_block(blk + 1, inode_disk_buf) != 0) return -1;
    memcpy(inode_disk_buf, (const uint8_t *)in + (block_size - o), o + isz - block_size);
    return write_one_block(blk + 1, inode_disk_buf);
}

static uint32_t bmap_indirect(uint32_t ind_blk, uint32_t idx) {
    if (read_one_block(ind_blk, io_buf) != 0) return 0;
    return ((uint32_t *)io_buf)[idx];
}

static uint32_t bmap_double(uint32_t dind, uint32_t idx) {
    uint32_t per = ptrs_per_block;
    uint32_t l1 = idx / per;
    uint32_t l2 = idx % per;
    uint32_t ib = bmap_indirect(dind, l1);
    if (!ib) return 0;
    return bmap_indirect(ib, l2);
}

static uint32_t bmap_triple(uint32_t tind, uint32_t idx) {
    uint32_t per = ptrs_per_block;
    uint32_t l1 = idx / (per * per);
    uint32_t r = idx % (per * per);
    uint32_t l2 = r / per;
    uint32_t l3 = r % per;
    uint32_t d = bmap_indirect(tind, l1);
    if (!d) return 0;
    uint32_t i = bmap_indirect(d, l2);
    if (!i) return 0;
    return bmap_indirect(i, l3);
}

static uint32_t inode_bmap(struct ext2_inode *ino, uint32_t file_blk) {
    if (file_blk < EXT2_NDIR_BLOCKS)
        return ino->i_block[file_blk];
    file_blk -= EXT2_NDIR_BLOCKS;
    if (file_blk < ptrs_per_block)
        return bmap_indirect(ino->i_block[EXT2_IND_BLOCK], file_blk);
    file_blk -= ptrs_per_block;
    if (file_blk < ptrs_per_block * ptrs_per_block)
        return bmap_double(ino->i_block[EXT2_DIND_BLOCK], file_blk);
    file_blk -= ptrs_per_block * ptrs_per_block;
    return bmap_triple(ino->i_block[EXT2_TIND_BLOCK], file_blk);
}

static int set_bmap_indirect(uint32_t ind_blk, uint32_t idx, uint32_t val) {
    if (read_one_block(ind_blk, io_buf) != 0) return -1;
    ((uint32_t *)io_buf)[idx] = val;
    return write_one_block(ind_blk, io_buf);
}

static int alloc_block(void);
static int free_block_nr(uint32_t b);

static int inode_set_bmap(struct ext2_inode *ino, uint32_t file_blk, uint32_t disk_blk) {
    if (file_blk < EXT2_NDIR_BLOCKS) {
        ino->i_block[file_blk] = disk_blk;
        return 0;
    }
    file_blk -= EXT2_NDIR_BLOCKS;
    if (file_blk >= ptrs_per_block)
        return -1;
    if (!ino->i_block[EXT2_IND_BLOCK]) {
        int nb = alloc_block();
        if (nb < 0) return -1;
        memset(io_buf, 0, block_size);
        if (write_one_block((uint32_t)nb, io_buf) != 0) return -1;
        ino->i_block[EXT2_IND_BLOCK] = (uint32_t)nb;
    }
    return set_bmap_indirect(ino->i_block[EXT2_IND_BLOCK], file_blk, disk_blk);
}

static void clear_bmap_bit(uint8_t *bmp, uint32_t bit) {
    bmp[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

static void set_bmap_bit(uint8_t *bmp, uint32_t bit) {
    bmp[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static int test_bmap_bit(const uint8_t *bmp, uint32_t bit) {
    return (bmp[bit / 8] >> (bit % 8)) & 1;
}

static int alloc_block(void) {
    for (uint32_t g = 0; g < num_groups; g++) {
        uint32_t bb = groups[g].bg_block_bitmap;
        if (read_one_block(bb, io_buf) != 0) continue;
        uint32_t nbits = sb.s_blocks_per_group;
        for (uint32_t i = 0; i < nbits; i++) {
            if (test_bmap_bit(io_buf, i)) continue;
            uint32_t absb = g * sb.s_blocks_per_group + i;
            if (absb < sb.s_first_data_block) continue;
            if (absb >= sb.s_blocks_count) continue;
            set_bmap_bit(io_buf, i);
            if (write_one_block(bb, io_buf) != 0) return -1;
            groups[g].bg_free_blocks_count--;
            sb.s_free_blocks_count--;
            return (int)absb;
        }
    }
    return -1;
}

static int free_block_nr(uint32_t b) {
    uint32_t g = b / sb.s_blocks_per_group;
    uint32_t i = b % sb.s_blocks_per_group;
    uint32_t bb = groups[g].bg_block_bitmap;
    if (read_one_block(bb, io_buf) != 0) return -1;
    if (!test_bmap_bit(io_buf, i)) return -1;
    clear_bmap_bit(io_buf, i);
    if (write_one_block(bb, io_buf) != 0) return -1;
    groups[g].bg_free_blocks_count++;
    sb.s_free_blocks_count++;
    return 0;
}

static int alloc_inode(void) {
    uint32_t first = sb.s_first_ino ? sb.s_first_ino : 11;
    for (uint32_t g = 0; g < num_groups; g++) {
        uint32_t ib = groups[g].bg_inode_bitmap;
        if (read_one_block(ib, io_buf) != 0) continue;
        for (uint32_t in = 0; in < sb.s_inodes_per_group; in++) {
            uint32_t ino = g * sb.s_inodes_per_group + in + 1;
            if (ino < first) continue;
            if (test_bmap_bit(io_buf, in)) continue;
            set_bmap_bit(io_buf, in);
            if (write_one_block(ib, io_buf) != 0) return -1;
            groups[g].bg_free_inodes_count--;
            sb.s_free_inodes_count--;
            return (int)ino;
        }
    }
    return -1;
}

static void free_inode_nr(uint32_t ino) {
    uint32_t g = inode_group(ino);
    uint32_t in = ino - g * sb.s_inodes_per_group - 1;
    uint32_t ib = groups[g].bg_inode_bitmap;
    if (read_one_block(ib, io_buf) != 0) return;
    if (test_bmap_bit(io_buf, in)) {
        clear_bmap_bit(io_buf, in);
        write_one_block(ib, io_buf);
        groups[g].bg_free_inodes_count++;
        sb.s_free_inodes_count++;
    }
}

static void free_dind_meta(uint32_t b) {
    if (!b) return;
    if (read_one_block(b, io_buf) != 0) return;
    for (uint32_t i = 0; i < ptrs_per_block; i++) {
        uint32_t x = ((uint32_t *)io_buf)[i];
        if (x) free_block_nr(x);
    }
    free_block_nr(b);
}

static void free_tind_meta(uint32_t b) {
    if (!b) return;
    if (read_one_block(b, io_buf) != 0) return;
    for (uint32_t i = 0; i < ptrs_per_block; i++) {
        uint32_t x = ((uint32_t *)io_buf)[i];
        if (x) free_dind_meta(x);
    }
    free_block_nr(b);
}

static void truncate_inode_blocks(struct ext2_inode *ino) {
    uint32_t maxb = (ino->i_size + block_size - 1) / block_size;
    for (uint32_t i = 0; i < maxb; i++) {
        uint32_t db = inode_bmap(ino, i);
        if (db) free_block_nr(db);
    }
    uint32_t i12 = ino->i_block[EXT2_IND_BLOCK];
    uint32_t i13 = ino->i_block[EXT2_DIND_BLOCK];
    uint32_t i14 = ino->i_block[EXT2_TIND_BLOCK];
    if (i12) free_block_nr(i12);
    if (i13) free_dind_meta(i13);
    if (i14) free_tind_meta(i14);
    memset(ino->i_block, 0, sizeof(ino->i_block));
    ino->i_size = 0;
    ino->i_blocks = 0;
}

static int inode_read_range(struct ext2_inode *ino, uint32_t off, uint8_t *buf, uint32_t len);
static void list_inode_dir(uint32_t dir_ino);

static void normalize_path(char *path) {
    for (char *p = path; *p; p++) {
        if (*p == '/' && *(p + 1) == '/') {
            char *q = p + 1;
            while (*q) { *q = *(q + 1); q++; }
            p--;
        }
    }
}

static int lookup_path(const char *path_in, uint32_t *out_ino, int expect_dir) {
    if (!fs_mounted) return -1;
    char path[256];
    if (path_in[0] == '/') {
        strncpy(path, path_in, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
    } else {
        if (cwd_path[0] == '/' && cwd_path[1] == 0) {
            path[0] = '/';
            strncpy(path + 1, path_in, sizeof(path) - 2);
            path[sizeof(path) - 1] = 0;
        } else {
            strcpy(path, cwd_path);
            strcat(path, "/");
            strcat(path, path_in);
        }
    }
    normalize_path(path);
    uint32_t ino = EXT2_ROOT_INO;
    if (path[0] == '/' && path[1] == 0) {
        *out_ino = EXT2_ROOT_INO;
        return 0;
    }
    const char *p = path + 1;
        while (*p) {
        char comp[128];
        int k = 0;
        while (*p && *p != '/' && k < 127) comp[k++] = *p++;
        comp[k] = 0;
        while (*p == '/') p++;
        if (!comp[0]) break;
        struct ext2_inode di;
        if (read_inode_raw(ino, &di) != 0) return -1;
        if ((di.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
        uint32_t pos = 0;
        int found = 0;
        while (pos < di.i_size) {
            uint8_t hdr[8];
            if (inode_read_range(&di, pos, hdr, 8) != 0) break;
            uint16_t rl = (uint16_t)(hdr[4] | (hdr[5] << 8));
            uint8_t nl = hdr[6];
            if (rl < 8 || pos + rl > di.i_size || rl > sizeof(dentry_read_buf)) break;
            if (inode_read_range(&di, pos, dentry_read_buf, rl) != 0) break;
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
            if (de->rec_len != rl || de->name_len != nl) break;
            char namebuf[256];
            if ((size_t)de->name_len + 1 >= sizeof(namebuf)) break;
            memcpy(namebuf, de->name, nl);
            namebuf[nl] = 0;
            if (strcmp(namebuf, comp) == 0) {
                ino = de->inode;
                found = 1;
                break;
            }
            pos += rl;
        }
        if (!found) return -1;
        (void)expect_dir;
    }
    struct ext2_inode fin;
    if (read_inode_raw(ino, &fin) != 0) return -1;
    if (expect_dir && (fin.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
    *out_ino = ino;
    return 0;
}

static int load_super_and_groups(void) {
    if (read_one_block(1, io_buf) != 0) return -1;
    memcpy(&sb, io_buf, sizeof(sb));
    if (sb.s_magic != EXT2_SUPER_MAGIC) return -1;
    block_size = 1024u << sb.s_log_block_size;
    if (block_size == 0 || block_size > 4096) return -1;
    ptrs_per_block = block_size / 4;
    if (sb.s_blocks_per_group == 0) return -1;
    num_groups = (sb.s_blocks_count + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    if (num_groups == 0 || num_groups > MAX_GROUPS) return -1;
    uint32_t total = num_groups * (uint32_t)sizeof(groups[0]);
    uint32_t pos = 0;
    while (pos < total) {
        uint32_t blk = 2 + pos / block_size;
        uint32_t off = pos % block_size;
        if (read_one_block(blk, io_buf) != 0) return -1;
        uint32_t chunk = block_size - off;
        if (chunk > total - pos) chunk = total - pos;
        memcpy((uint8_t *)groups + pos, io_buf + off, chunk);
        pos += chunk;
    }
    return 0;
}

static void inode_recalc_blocks(struct ext2_inode *ino) {
    uint32_t n = (ino->i_size + block_size - 1) / block_size;
    ino->i_blocks = n * (block_size / 512);
}

static int inode_read_range(struct ext2_inode *ino, uint32_t off, uint8_t *buf, uint32_t len) {
    uint32_t done = 0;
    while (done < len) {
        if (off + done >= ino->i_size) return -1;
        uint32_t fo = off + done;
        uint32_t fb = fo / block_size;
        uint32_t bo = fo % block_size;
        uint32_t db = inode_bmap(ino, fb);
        if (!db) return -1;
        if (read_one_block(db, io_buf) != 0) return -1;
        uint32_t chunk = block_size - bo;
        uint32_t rem = ino->i_size - fo;
        if (chunk > rem) chunk = rem;
        if (chunk > len - done) chunk = len - done;
        memcpy(buf + done, io_buf + bo, chunk);
        done += chunk;
    }
    return 0;
}

static int inode_write_range(uint32_t ino_num, struct ext2_inode *ino, uint32_t off, const uint8_t *buf, uint32_t len) {
    uint32_t end = off + len;
    if (end > ino->i_size) ino->i_size = end;
    uint32_t done = 0;
    while (done < len) {
        uint32_t fo = off + done;
        uint32_t fb = fo / block_size;
        uint32_t bo = fo % block_size;
        uint32_t db = inode_bmap(ino, fb);
        if (!db) {
            int nb = alloc_block();
            if (nb < 0) return -1;
            if (inode_set_bmap(ino, fb, (uint32_t)nb) != 0) return -1;
            db = (uint32_t)nb;
            memset(io_buf, 0, block_size);
            if (write_one_block(db, io_buf) != 0) return -1;
        }
        if (read_one_block(db, io_buf) != 0) return -1;
        uint32_t chunk = block_size - bo;
        if (chunk > len - done) chunk = len - done;
        memcpy(io_buf + bo, buf + done, chunk);
        if (write_one_block(db, io_buf) != 0) return -1;
        done += chunk;
    }
    inode_recalc_blocks(ino);
    ino->i_mtime = now_unix();
    return write_inode_raw(ino_num, ino);
}

static uint16_t rec_len_round(uint8_t name_len) {
    uint16_t r = (uint16_t)(8 + name_len);
    r = (uint16_t)((r + 3u) & ~3u);
    return r < 8 ? 8 : r;
}

static int dir_add_entry(uint32_t dir_ino, const char *name, uint32_t target_ino, uint8_t ftype) {
    struct ext2_inode di;
    if (read_inode_raw(dir_ino, &di) != 0) return -1;
    if ((di.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
    uint8_t nl = (uint8_t)strlen(name);
    uint16_t rl = rec_len_round(nl);
    uint8_t ent[512];
    if (rl > sizeof(ent)) return -1;
    struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)ent;
    de->inode = target_ino;
    de->rec_len = rl;
    de->name_len = nl;
    de->file_type = ftype;
    memcpy(de->name, name, nl);
    uint32_t pos = di.i_size;
    return inode_write_range(dir_ino, &di, pos, ent, rl);
}

static int dir_remove_entry(uint32_t dir_ino, const char *name) {
    struct ext2_inode di;
    if (read_inode_raw(dir_ino, &di) != 0) return -1;
    uint8_t tmp[8192];
    if (di.i_size > sizeof(tmp)) return -1;
    if (inode_read_range(&di, 0, tmp, di.i_size) != 0) return -1;
    uint32_t pos = 0, wr = 0;
    while (pos < di.i_size) {
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(tmp + pos);
        if (de->rec_len < 8) return -1;
        char nb[256];
        memcpy(nb, de->name, de->name_len);
        nb[de->name_len] = 0;
        if (strcmp(nb, name) != 0) {
            memmove(tmp + wr, tmp + pos, de->rec_len);
            wr += de->rec_len;
        }
        pos += de->rec_len;
    }
    truncate_inode_blocks(&di);
    if (read_inode_raw(dir_ino, &di) != 0) return -1;
    di.i_size = wr;
    if (write_inode_raw(dir_ino, &di) != 0) return -1;
    if (read_inode_raw(dir_ino, &di) != 0) return -1;
    if (wr > 0 && inode_write_range(dir_ino, &di, 0, tmp, wr) != 0) return -1;
    return 0;
}

static int split_parent_name(const char *abs_path, char *parent, char *name) {
    const char *last = abs_path;
    for (const char *p = abs_path; *p; p++)
        if (*p == '/') last = p;
    if (last == abs_path && *abs_path == '/') {
        parent[0] = '/';
        parent[1] = 0;
        strcpy(name, abs_path + 1);
        return 0;
    }
    size_t plen = (size_t)(last - abs_path);
    if (plen >= 256) return -1;
    memcpy(parent, abs_path, plen);
    parent[plen] = 0;
    strcpy(name, last + 1);
    if (parent[0] == 0) {
        parent[0] = '/';
        parent[1] = 0;
    }
    return 0;
}

static int path_to_abs(const char *path_in, char *out, size_t olen) {
    if (path_in[0] == '/') {
        strncpy(out, path_in, olen - 1);
        out[olen - 1] = 0;
    } else {
        if (cwd_path[0] == '/' && cwd_path[1] == 0) {
            out[0] = '/';
            strncpy(out + 1, path_in, olen - 2);
            out[olen - 1] = 0;
        } else {
            strncpy(out, cwd_path, olen - 1);
            out[olen - 1] = 0;
            size_t len = strlen(out);
            if (len + 1 < olen) {
                out[len] = '/';
                out[len + 1] = 0;
                strncpy(out + len + 1, path_in, olen - len - 2);
                out[olen - 1] = 0;
            }
        }
    }
    normalize_path(out);
    return 0;
}

void fs_init(void) {
    fs_mounted = 0;
    cwd_ino = EXT2_ROOT_INO;
    cwd_path[0] = '/';
    cwd_path[1] = 0;
    memset(&sb, 0, sizeof(sb));
    block_size = 1024;
    ptrs_per_block = 256;
}

void fs_ensure_mounted(void) {
    if (partition_offset == 0) return;
    if (fs_mounted) return;
    fs_load_from_disk();
}

void fs_sync_to_disk(void) {
    if (!fs_mounted || partition_offset == 0) return;
    sync_super();
    sync_group_desc();
    ata_flush();
}

int fs_is_mounted(void) { return fs_mounted; }

void fs_load_from_disk(void) {
    if (partition_offset == 0) return;
    if (load_super_and_groups() == 0) {
        fs_mounted = 1;
        cwd_ino = EXT2_ROOT_INO;
        strcpy(cwd_path, "/");
    } else
        fs_mounted = 0;
}

int fs_has_user_content(void) {
    if (!fs_mounted) return 0;
    struct ext2_inode ri;
    if (read_inode_raw(EXT2_ROOT_INO, &ri) != 0) return 0;
    uint32_t pos = 0;
    int n = 0;
    while (pos < ri.i_size) {
        uint8_t hdr[8];
        if (inode_read_range(&ri, pos, hdr, 8) != 0) break;
        uint16_t rl = (uint16_t)(hdr[4] | (hdr[5] << 8));
        if (rl < 8 || pos + rl > ri.i_size || rl > sizeof(dentry_read_buf)) break;
        if (inode_read_range(&ri, pos, dentry_read_buf, rl) != 0) break;
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
        if (de->rec_len != rl) break;
        char nb[256];
        if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
        memcpy(nb, de->name, de->name_len);
        nb[de->name_len] = 0;
        if (strcmp(nb, ".") != 0 && strcmp(nb, "..") != 0) n++;
        pos += rl;
    }
    return n > 0;
}

int fs_exists(const char *path) {
    uint32_t ino;
    return lookup_path(path, &ino, 0) == 0 ? 1 : 0;
}

int fs_is_directory(const char *name) {
    uint32_t ino;
    if (lookup_path(name, &ino, 0) != 0) return 0;
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) return 0;
    return (in.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

void fs_get_cwd(char *buf, int len) {
    strncpy(buf, cwd_path, (size_t)len - 1);
    buf[len - 1] = 0;
}

int fs_cd(const char *path) {
    if (path[0] == 0 || strcmp(path, "/") == 0) {
        cwd_ino = EXT2_ROOT_INO;
        cwd_path[0] = '/';
        cwd_path[1] = 0;
        return 0;
    }
    uint32_t ino;
    if (lookup_path(path, &ino, 1) != 0) return -1;
    cwd_ino = ino;
    char abs[256];
    path_to_abs(path, abs, sizeof(abs));
    strncpy(cwd_path, abs, sizeof(cwd_path) - 1);
    cwd_path[sizeof(cwd_path) - 1] = 0;
    return 0;
}

int fs_mkdir(const char *name) { return fs_create(name, 1); }

int fs_create(const char *name, uint8_t is_dir) {
    if (!fs_mounted) return -1;
    char abs[256];
    path_to_abs(name, abs, sizeof(abs));
    if (fs_exists(abs)) return -1;
    char parent[256], base[128];
    if (split_parent_name(abs, parent, base) != 0) return -1;
    uint32_t pin;
    if (lookup_path(parent, &pin, 1) != 0) return -1;
    int ino = alloc_inode();
    if (ino < 0) return -1;
    struct ext2_inode ni;
    memset(&ni, 0, sizeof(ni));
    ni.i_mode = (uint16_t)(is_dir ? (EXT2_S_IFDIR | 0777) : (EXT2_S_IFREG | 0666));
    ni.i_uid = 0;
    ni.i_size = 0;
    ni.i_links_count = is_dir ? (uint16_t)2 : (uint16_t)1;
    ni.i_blocks = 0;
    uint32_t t = now_unix();
    ni.i_ctime = ni.i_mtime = ni.i_atime = t;
    if (write_inode_raw((uint32_t)ino, &ni) != 0) return -1;
    if (is_dir) {
        struct ext2_inode tmp;
        if (read_inode_raw((uint32_t)ino, &tmp) != 0) return -1;
        memset(io_buf, 0, block_size);
        struct ext2_dir_entry_2 *d = (struct ext2_dir_entry_2 *)io_buf;
        d->inode = (uint32_t)ino;
        d->rec_len = 12;
        d->name_len = 1;
        d->file_type = EXT2_FT_DIR;
        d->name[0] = '.';
        struct ext2_dir_entry_2 *d2 = (struct ext2_dir_entry_2 *)(io_buf + 12);
        d2->inode = pin;
        d2->rec_len = (uint16_t)(block_size - 12);
        d2->name_len = 2;
        d2->file_type = EXT2_FT_DIR;
        d2->name[0] = '.';
        d2->name[1] = '.';
        tmp.i_size = block_size;
        tmp.i_blocks = block_size / 512;
        int db = alloc_block();
        if (db < 0) return -1;
        tmp.i_block[0] = (uint32_t)db;
        if (write_one_block((uint32_t)db, io_buf) != 0) return -1;
        if (write_inode_raw((uint32_t)ino, &tmp) != 0) return -1;
    }
    if (dir_add_entry(pin, base, (uint32_t)ino, (uint8_t)(is_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE)) != 0) return -1;
    struct ext2_inode pdi;
    if (read_inode_raw(pin, &pdi) != 0) return -1;
    if (is_dir) pdi.i_links_count++;
    write_inode_raw(pin, &pdi);
    fs_sync_to_disk();
    return 0;
}

int fs_delete(const char *name) {
    if (!fs_mounted) return -1;
    char abs[256];
    path_to_abs(name, abs, sizeof(abs));
    uint32_t ino;
    if (lookup_path(abs, &ino, 0) != 0) return -1;
    if (ino == EXT2_ROOT_INO) return -1;
    struct ext2_inode target;
    if (read_inode_raw(ino, &target) != 0) return -1;
    int is_dir = ((target.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR);
    if (is_dir) {
        uint32_t pos = 0;
        int cnt = 0;
        while (pos < target.i_size) {
            uint8_t hdr[8];
            if (inode_read_range(&target, pos, hdr, 8) != 0) break;
            uint16_t rl = (uint16_t)(hdr[4] | (hdr[5] << 8));
            if (rl < 8 || pos + rl > target.i_size || rl > sizeof(dentry_read_buf)) break;
            if (inode_read_range(&target, pos, dentry_read_buf, rl) != 0) break;
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
            if (de->rec_len != rl) break;
            char nb[256];
            if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
            memcpy(nb, de->name, de->name_len);
            nb[de->name_len] = 0;
            if (strcmp(nb, ".") != 0 && strcmp(nb, "..") != 0) cnt++;
            pos += rl;
        }
        if (cnt > 0) return -1;
    }
    char parent[256], base[128];
    if (split_parent_name(abs, parent, base) != 0) return -1;
    uint32_t pin;
    if (lookup_path(parent, &pin, 1) != 0) return -1;
    if (dir_remove_entry(pin, base) != 0) return -1;
    truncate_inode_blocks(&target);
    if (is_dir) {
        struct ext2_inode pdi;
        if (read_inode_raw(pin, &pdi) != 0) return -1;
        if (pdi.i_links_count > 0) pdi.i_links_count--;
        write_inode_raw(pin, &pdi);
    }
    free_inode_nr(ino);
    memset(&target, 0, sizeof(target));
    write_inode_raw(ino, &target);
    fs_sync_to_disk();
    return 0;
}

int fs_remove(const char *name) { return fs_delete(name); }

int fs_open(const char *name, uint8_t *data, uint32_t buf_max, uint32_t *size_out) {
    char abs[256];
    path_to_abs(name, abs, sizeof(abs));
    uint32_t ino;
    if (lookup_path(abs, &ino, 0) != 0) return -1;
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) return -1;
    if ((in.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) return -1;
    uint32_t sz = in.i_size;
    if (sz > buf_max) sz = buf_max;
    if (inode_read_range(&in, 0, data, sz) != 0) return -1;
    *size_out = sz;
    return 0;
}

int fs_write(const char *name, const uint8_t *data, uint32_t size) {
    if (!fs_mounted) return -1;
    char abs[256];
    path_to_abs(name, abs, sizeof(abs));
    uint32_t ino;
    if (lookup_path(abs, &ino, 0) != 0) return -1;
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) return -1;
    if ((in.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        truncate_inode_blocks(&in);
        if (read_inode_raw(ino, &in) != 0) return -1;
        in.i_size = 0;
        write_inode_raw(ino, &in);
        if (read_inode_raw(ino, &in) != 0) return -1;
        if (size == 0) {
            write_inode_raw(ino, &in);
            fs_sync_to_disk();
            return 0;
        }
        if (inode_write_range(ino, &in, 0, data, size) != 0) return -1;
        fs_sync_to_disk();
        return 0;
    }
    return -1;
}

void fs_list(void) {
    if (!fs_mounted) return;
    list_inode_dir(cwd_ino);
}

int fs_change_dir(const char *path) { return fs_cd(path); }

static int ext2_format_partition(uint32_t part_sectors) {
    uint32_t total_blocks = (part_sectors * 512) / 1024;
    if (total_blocks < 24) return -1;

    const uint32_t ipg = 128;
    uint32_t bpbg = 8192;
    if (total_blocks < bpbg) bpbg = total_blocks;
    uint32_t ng = (total_blocks + bpbg - 1) / bpbg;
    if (ng == 0 || ng > MAX_GROUPS) return -1;

    uint32_t itb = (ipg * EXT2_GOOD_OLD_INODE_SIZE + 1023) / 1024;
    uint32_t gdt_blocks = (ng * 32u + 1023) / 1024;

    uint32_t bb0 = 2 + gdt_blocks;
    uint32_t ib0 = bb0 + 1;
    uint32_t it0 = ib0 + 1;
    uint32_t first_data = it0 + itb;

    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = ng * ipg;
    sb.s_blocks_count = total_blocks;
    sb.s_r_blocks_count = 0;
    sb.s_log_block_size = 0;
    sb.s_blocks_per_group = bpbg;
    sb.s_frags_per_group = bpbg;
    sb.s_inodes_per_group = ipg;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = 1;
    sb.s_rev_level = 1;
    sb.s_first_ino = 11;
    sb.s_inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    sb.s_first_data_block = first_data;

    memset(groups, 0, sizeof(groups));

    for (uint32_t g = 0; g < ng; g++) {
        uint32_t gs = g * bpbg;
        uint32_t bb, ib, it;
        if (g == 0) {
            bb = bb0;
            ib = ib0;
            it = it0;
        } else {
            bb = gs;
            ib = gs + 1;
            it = gs + 2;
        }
        groups[g].bg_block_bitmap = bb;
        groups[g].bg_inode_bitmap = ib;
        groups[g].bg_inode_table = it;
        uint32_t first_d = it + itb;
        uint32_t used_in_group = first_d - gs;
        if (used_in_group > bpbg) used_in_group = bpbg;
        groups[g].bg_free_blocks_count = (uint16_t)(bpbg - used_in_group);
        groups[g].bg_free_inodes_count = (uint16_t)(ipg - (g == 0 ? 10u : 0u));
    }

    memset(io_buf, 0, 1024);
    write_one_block(0, io_buf);

    uint32_t total_gdt = ng * (uint32_t)sizeof(groups[0]);
    uint32_t p = 0;
    while (p < total_gdt) {
        uint32_t blk = 2 + p / 1024;
        uint32_t off = p % 1024;
        read_one_block(blk, io_buf);
        uint32_t ch = 1024 - off;
        if (ch > total_gdt - p) ch = total_gdt - p;
        memcpy(io_buf + off, (uint8_t *)groups + p, ch);
        write_one_block(blk, io_buf);
        p += ch;
    }

    for (uint32_t g = 0; g < ng; g++) {
        uint32_t gs = g * bpbg;
        uint32_t bb = groups[g].bg_block_bitmap;
        memset(io_buf, 0, 1024);
        for (uint32_t bi = 0; bi < bpbg && gs + bi < total_blocks; bi++) {
            uint32_t ab = gs + bi;
            uint32_t it = groups[g].bg_inode_table;
            if (ab < it + itb && ab >= gs)
                set_bmap_bit(io_buf, bi);
        }
        if (g == 0) {
            for (uint32_t b = 0; b < first_data && b < bpbg; b++)
                set_bmap_bit(io_buf, b);
        }
        write_one_block(bb, io_buf);
    }

    uint32_t free_b = 0;
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t bb = groups[g].bg_block_bitmap;
        read_one_block(bb, io_buf);
        uint32_t gs = g * bpbg;
        for (uint32_t bi = 0; bi < bpbg && gs + bi < total_blocks; bi++) {
            if (!test_bmap_bit(io_buf, bi)) free_b++;
        }
    }
    sb.s_free_blocks_count = free_b;
    sb.s_free_inodes_count = sb.s_inodes_count - 10;

    memset(io_buf, 0, 1024);
    memcpy(io_buf, &sb, sizeof(sb));
    write_one_block(1, io_buf);

    for (uint32_t g = 0; g < ng; g++) {
        uint32_t ib = groups[g].bg_inode_bitmap;
        memset(io_buf, 0, 1024);
        for (uint32_t i = 0; i < 10 && i < ipg; i++)
            set_bmap_bit(io_buf, i);
        write_one_block(ib, io_buf);
    }

    for (uint32_t g = 0; g < ng; g++) {
        uint32_t it = groups[g].bg_inode_table;
        for (uint32_t b = 0; b < itb; b++) {
            memset(io_buf, 0, 1024);
            write_one_block(it + b, io_buf);
        }
    }

    uint32_t root_data = first_data;
    struct ext2_inode root;
    memset(&root, 0, sizeof(root));
    root.i_mode = (uint16_t)(EXT2_S_IFDIR | 0777);
    root.i_size = 1024;
    root.i_links_count = 2;
    root.i_blocks = 2;
    uint32_t tt = now_unix();
    root.i_ctime = root.i_mtime = root.i_atime = tt;
    root.i_block[0] = root_data;
    if (write_inode_raw(EXT2_ROOT_INO, &root) != 0) return -1;

    memset(io_buf, 0, 1024);
    struct ext2_dir_entry_2 *d1 = (struct ext2_dir_entry_2 *)io_buf;
    d1->inode = EXT2_ROOT_INO;
    d1->rec_len = 12;
    d1->name_len = 1;
    d1->file_type = EXT2_FT_DIR;
    d1->name[0] = '.';
    struct ext2_dir_entry_2 *d2 = (struct ext2_dir_entry_2 *)(io_buf + 12);
    d2->inode = EXT2_ROOT_INO;
    d2->rec_len = 1012;
    d2->name_len = 2;
    d2->file_type = EXT2_FT_DIR;
    d2->name[0] = '.';
    d2->name[1] = '.';
    write_one_block(root_data, io_buf);

    groups[0].bg_used_dirs_count = 1;
    p = 0;
    while (p < total_gdt) {
        uint32_t blk = 2 + p / 1024;
        uint32_t off = p % 1024;
        read_one_block(blk, io_buf);
        uint32_t ch = 1024 - off;
        if (ch > total_gdt - p) ch = total_gdt - p;
        memcpy(io_buf + off, (uint8_t *)groups + p, ch);
        write_one_block(blk, io_buf);
        p += ch;
    }

    memset(io_buf, 0, 1024);
    memcpy(io_buf, &sb, sizeof(sb));
    write_one_block(1, io_buf);

    if (load_super_and_groups() != 0) return -1;
    fs_mounted = 1;
    cwd_ino = EXT2_ROOT_INO;
    strcpy(cwd_path, "/");
    return 0;
}

int fs_format_partition(uint32_t part_sectors) {
    return ext2_format_partition(part_sectors);
}

void cmd_rm(const char *arg) {
    while (*arg == ' ') arg++;
    if (strncmp(arg, "-d ", 3) == 0) {
        const char *dirname = arg + 3;
        while (*dirname == ' ') dirname++;
        if (!fs_is_directory(dirname))
            vga_write("Not a directory or doesn't exist\n", VGA_COLOR_LIGHT_RED);
        else if (fs_delete(dirname) == 0)
            vga_write("Directory removed\n", VGA_COLOR_LIGHT_GREEN);
        else
            vga_write("Failed to remove directory\n", VGA_COLOR_LIGHT_RED);
    } else if (strncmp(arg, "-f ", 3) == 0) {
        const char *filename = arg + 3;
        while (*filename == ' ') filename++;
        if (fs_is_directory(filename))
            vga_write("Is a directory (use -d)\n", VGA_COLOR_LIGHT_RED);
        else if (fs_delete(filename) == 0)
            vga_write("File removed\n", VGA_COLOR_LIGHT_GREEN);
        else
            vga_write("File not found\n", VGA_COLOR_LIGHT_RED);
    } else
        vga_write("Usage: rm -d <dir> | rm -f <file>\n", VGA_COLOR_LIGHT_RED);
}

static void list_inode_dir(uint32_t dir_ino) {
    struct ext2_inode di;
    if (read_inode_raw(dir_ino, &di) != 0) return;
    uint32_t pos = 0;
    while (pos < di.i_size) {
        uint8_t hdr[8];
        if (inode_read_range(&di, pos, hdr, 8) != 0) break;
        uint16_t rl = (uint16_t)(hdr[4] | (hdr[5] << 8));
        if (rl < 8 || pos + rl > di.i_size || rl > sizeof(dentry_read_buf)) break;
        if (inode_read_range(&di, pos, dentry_read_buf, rl) != 0) break;
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
        if (de->rec_len != rl) break;
        char nb[256];
        if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
        memcpy(nb, de->name, de->name_len);
        nb[de->name_len] = 0;
        if (strcmp(nb, ".") == 0 || strcmp(nb, "..") == 0) {
            pos += rl;
            continue;
        }
        vga_write(nb, VGA_COLOR_LIGHT_GREY);
        if (de->file_type == EXT2_FT_DIR) vga_write("/", VGA_COLOR_LIGHT_CYAN);
        vga_write("  ", VGA_COLOR_LIGHT_GREY);
        pos += rl;
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void fs_list_at_path(const char *ext2_abs_dir) {
    if (!fs_mounted) {
        vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t ino;
    if (lookup_path(ext2_abs_dir, &ino, 1) != 0) {
        vga_write("(bad ext2 path)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    list_inode_dir(ino);
}

static void fmt_mode_ext2(uint16_t mode, char *o) {
    int isdir = ((mode & EXT2_S_IFMT) == EXT2_S_IFDIR);
    o[0] = isdir ? 'd' : '-';
    uint16_t perm = mode & 0777;
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        o[1 + i] = (perm & (1u << (8 - i))) ? rwx[i] : '-';
    o[10] = 0;
}

static void list_inode_dir_long(uint32_t dir_ino, int show_all) {
    struct ext2_inode di;
    if (read_inode_raw(dir_ino, &di) != 0) return;
    uint32_t pos = 0;
    while (pos < di.i_size) {
        uint8_t hdr[8];
        if (inode_read_range(&di, pos, hdr, 8) != 0) break;
        uint16_t rl = (uint16_t)(hdr[4] | (hdr[5] << 8));
        if (rl < 8 || pos + rl > di.i_size || rl > sizeof(dentry_read_buf)) break;
        if (inode_read_range(&di, pos, dentry_read_buf, rl) != 0) break;
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
        if (de->rec_len != rl) break;
        char nb[256];
        if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
        memcpy(nb, de->name, de->name_len);
        nb[de->name_len] = 0;
        if (!show_all && (strcmp(nb, ".") == 0 || strcmp(nb, "..") == 0)) {
            pos += rl;
            continue;
        }
        struct ext2_inode fin;
        char md[12];
        if (read_inode_raw(de->inode, &fin) != 0) {
            pos += rl;
            continue;
        }
        fmt_mode_ext2(fin.i_mode, md);
        vga_write(md, VGA_COLOR_LIGHT_GREY);
        vga_write(" 1 root root ", VGA_COLOR_LIGHT_GREY);
        char szb[16];
        int_to_str((int)fin.i_size, szb);
        vga_write(szb, VGA_COLOR_LIGHT_CYAN);
        vga_write(" ", VGA_COLOR_LIGHT_GREY);
        int_to_str((int)fin.i_mtime, szb);
        vga_write(szb, VGA_COLOR_LIGHT_BROWN);
        vga_write(" ", VGA_COLOR_LIGHT_GREY);
        vga_write(nb, VGA_COLOR_LIGHT_GREEN);
        if (de->file_type == EXT2_FT_DIR) vga_write("/", VGA_COLOR_LIGHT_CYAN);
        vga_write("\n", VGA_COLOR_LIGHT_GREY);
        pos += rl;
    }
}

void fs_list_long_at_path(const char *ext2_abs_dir, int show_all) {
    if (!fs_mounted) {
        vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t ino;
    if (lookup_path(ext2_abs_dir, &ino, 1) != 0) {
        vga_write("(bad ext2 path)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    list_inode_dir_long(ino, show_all);
}

int fs_mv_path(const char *ext2_from_abs, const char *ext2_to_abs) {
    if (!fs_mounted) return -1;
    uint32_t fino;
    if (lookup_path(ext2_from_abs, &fino, 0) != 0) return -1;
    struct ext2_inode fi;
    if (read_inode_raw(fino, &fi) != 0) return -1;
    if ((fi.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) return -1;
    uint8_t buf[8192];
    uint32_t sz = 0;
    if (fs_open(ext2_from_abs, buf, sizeof(buf), &sz) != 0) return -1;
    if (fs_exists(ext2_to_abs)) {
        if (fs_delete(ext2_to_abs) != 0) return -1;
    }
    if (fs_create(ext2_to_abs, 0) != 0) return -1;
    if (fs_write(ext2_to_abs, buf, sz) != 0) return -1;
    if (fs_delete(ext2_from_abs) != 0) return -1;
    return 0;
}

void cmd_ls(void) { fs_list(); }

void fs_cat_path(const char *ext2_abs) {
    if (!fs_mounted) {
        vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t ino;
    if (lookup_path(ext2_abs, &ino, 0) != 0) {
        vga_write("File not found\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) return;
    if ((in.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
        vga_write("Is a directory\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t pos = 0;
    while (pos < in.i_size) {
        uint8_t line[80];
        uint32_t n = in.i_size - pos;
        if (n > sizeof(line) - 1) n = sizeof(line) - 1;
        if (inode_read_range(&in, pos, line, n) != 0) break;
        line[n] = 0;
        vga_write((char *)line, VGA_COLOR_LIGHT_GREY);
        pos += n;
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void cmd_cat(const char *fname) {
    char abs[256];
    path_to_abs(fname, abs, sizeof(abs));
    fs_cat_path(abs);
}

void cmd_touch(const char *fname) {
    if (fs_create(fname, 0) == 0)
        vga_write("File created\n", VGA_COLOR_LIGHT_GREEN);
    else
        vga_write("Failed to create file\n", VGA_COLOR_LIGHT_RED);
}

void cmd_pwd(void) {
    char buf[256];
    fs_get_cwd(buf, sizeof(buf));
    vga_write(buf, VGA_COLOR_LIGHT_GREY);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

static void editor_draw(const char *buffer, int cursor_pos, int start_row, int *out_cursor_x, int *out_cursor_y) {
    int row = start_row;
    int col = 0;
    int current_pos = 0;
    int found_cursor = 0;
    for (int r = start_row; r <= VGA_LAST_TEXT_ROW; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_MEMORY[r * VGA_WIDTH + c] = (VGA_COLOR_LIGHT_GREY << 8) | ' ';
    for (int i = 0; buffer[i] && row <= VGA_LAST_TEXT_ROW; i++) {
        if (buffer[i] == '\n') {
            row++;
            col = 0;
            current_pos++;
            continue;
        }
        if (row <= VGA_LAST_TEXT_ROW && col < VGA_WIDTH) {
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
            if (buffer[i] == '\n') {
                r++;
                c = 0;
            } else if (pos == cursor_pos)
                break;
            else
                c++;
            pos++;
        }
        if (r > VGA_LAST_TEXT_ROW) r = VGA_LAST_TEXT_ROW;
        if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
        *out_cursor_x = c;
        *out_cursor_y = r;
    }
}

void cmd_ynan_at(const char *abs_path) {
    uint16_t saved_screen[VGA_WIDTH * VGA_HEIGHT];
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        saved_screen[i] = VGA_MEMORY[i];
    uint8_t saved_cx, saved_cy;
    vga_get_cursor(&saved_cx, &saved_cy);
    vga_clear(VGA_COLOR_BLACK | (VGA_COLOR_BLACK << 4));
    vga_set_cursor(0, 0);
    vga_write("Editing: ", VGA_COLOR_LIGHT_GREY);
    vga_write(abs_path, VGA_COLOR_LIGHT_BLUE);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    vga_write("Arrow keys to move, ESC to save, '.' + Enter on empty line to save\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("------------------------------------------------------------\n", VGA_COLOR_LIGHT_GREY);
#define MAX_EDIT_SIZE 8192
    char *buffer = (char *)(uintptr_t)0x200000;
    int size = 0, cursor = 0;
    uint32_t loaded_size = 0;
    if (fs_open(abs_path, (uint8_t *)buffer, MAX_EDIT_SIZE - 1, &loaded_size) == 0) {
        size = (int)loaded_size;
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
            case 0x4B:
                if (cursor > 0) cursor--;
                break;
            case 0x4D:
                if (cursor < size) cursor++;
                break;
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
            default:
                break;
            }
            editor_draw(buffer, cursor, 2, &cursor_x, &cursor_y);
            vga_set_cursor(cursor_x, cursor_y);
            continue;
        }
        char c = scancode_to_char(sc);
        if (c == 0x1B) {
            running = 0;
            break;
        }
        if (c == '\n') {
            int line_start = cursor;
            while (line_start > 0 && buffer[line_start - 1] != '\n') line_start--;
            if (cursor == line_start && (cursor == 0 || buffer[cursor - 1] == '\n')) {
                if (cursor > 0 && buffer[cursor - 1] == '.' && (cursor - 1 == line_start)) {
                    memmove(buffer + cursor - 1, buffer + cursor, (size_t)(size - cursor));
                    size--;
                    cursor--;
                    buffer[size] = '\0';
                    running = 0;
                    break;
                }
            }
            if (size < MAX_EDIT_SIZE - 1) {
                memmove(buffer + cursor + 1, buffer + cursor, (size_t)(size - cursor));
                buffer[cursor] = '\n';
                size++;
                cursor++;
                buffer[size] = '\0';
            }
        } else if (c == '\b' && cursor > 0) {
            memmove(buffer + cursor - 1, buffer + cursor, (size_t)(size - cursor));
            size--;
            cursor--;
            buffer[size] = '\0';
        } else if (c >= ' ' && c <= '~' && size < MAX_EDIT_SIZE - 1) {
            memmove(buffer + cursor + 1, buffer + cursor, (size_t)(size - cursor));
            buffer[cursor] = c;
            size++;
            cursor++;
            buffer[size] = '\0';
        }
        editor_draw(buffer, cursor, 2, &cursor_x, &cursor_y);
        vga_set_cursor(cursor_x, cursor_y);
    }
    if (running == 0) {
        vga_set_cursor(0, VGA_LAST_TEXT_ROW);
        vga_write("\nSaving...\n", VGA_COLOR_LIGHT_GREEN);
        if (size > 0) {
            if (fs_write(abs_path, (uint8_t *)buffer, (uint32_t)size) == 0)
                vga_write("Saved.\n", VGA_COLOR_LIGHT_GREEN);
            else
                vga_write("Failed to save file.\n", VGA_COLOR_LIGHT_RED);
        } else {
            fs_delete(abs_path);
            vga_write("Empty file deleted.\n", VGA_COLOR_LIGHT_GREEN);
        }
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = saved_screen[i];
    vga_set_cursor(saved_cx, saved_cy);
    vga_taskbar_refresh();
}

void cmd_ynan(const char *fname) {
    char abs[256];
    path_to_abs(fname, abs, sizeof(abs));
    cmd_ynan_at(abs);
}
