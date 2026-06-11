#include "fs.h"
#include "drivers.h"
#include "kernel.h"
#include "string.h"
#include "types.h"
#include "log.h"

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
static int free_block_nr(uint32_t b);
static uint32_t now_unix(void);

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

    
    if (read_one_block(blk, inode_disk_buf) != 0) {
        if (ino == 12) vga_write("[debug] read_one_block failed\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    
    if (o + isz > block_size) {
        memcpy(out, inode_disk_buf + o, block_size - o);
        if (read_one_block(blk + 1, inode_disk_buf) != 0) {
            if (ino == 12) vga_write("[debug] read_one_block (second) failed\n", VGA_COLOR_LIGHT_RED);
            return -1;
        }
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

static uint32_t next_block = 100; 
static int alloc_block(void) {
    // Находим первый неиспользуемый блок, начиная с next_block
    for (uint32_t b = next_block; b < sb.s_blocks_count; b++) {
        uint32_t g = b / sb.s_blocks_per_group;
        uint32_t i = b % sb.s_blocks_per_group;
        uint32_t bb = groups[g].bg_block_bitmap;
        if (read_one_block(bb, io_buf) != 0) continue;
        if (!test_bmap_bit(io_buf, i)) {
            // Блок свободен
            set_bmap_bit(io_buf, i);
            if (write_one_block(bb, io_buf) != 0) return -1;
            groups[g].bg_free_blocks_count--;
            sb.s_free_blocks_count--;
            next_block = b + 1;
            return (int)b;
        }
    }
    return -1;
}

static int free_block_nr(uint32_t b) {
    if (b == 0 || b >= sb.s_blocks_count) return -1;
    uint32_t g = b / sb.s_blocks_per_group;
    if (g >= num_groups) return -1;
    uint32_t i = b % sb.s_blocks_per_group;
    uint32_t bb = groups[g].bg_block_bitmap;
    if (bb == 0) return -1;
    if (read_one_block(bb, io_buf) != 0) return -1;
    if (!test_bmap_bit(io_buf, i)) return -1;  // уже свободен
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
        if (db && db < sb.s_blocks_count)   // добавить проверку db < sb.s_blocks_count
            free_block_nr(db);
    }
    uint32_t i12 = ino->i_block[EXT2_IND_BLOCK];
    uint32_t i13 = ino->i_block[EXT2_DIND_BLOCK];
    uint32_t i14 = ino->i_block[EXT2_TIND_BLOCK];
    if (i12 && i12 < sb.s_blocks_count) free_block_nr(i12);
    if (i13 && i13 < sb.s_blocks_count) free_dind_meta(i13);
    if (i14 && i14 < sb.s_blocks_count) free_tind_meta(i14);
    memset(ino->i_block, 0, sizeof(ino->i_block));
    ino->i_size = 0;
    ino->i_blocks = 0;
}

static int inode_read_range(struct ext2_inode *ino, uint32_t off, uint8_t *buf, uint32_t len) {
    if (!ino || !buf || len == 0) return -1;
    if (off >= ino->i_size) return -1;

    uint32_t done = 0;
    while (done < len && (off + done) < ino->i_size) {
        uint32_t pos = off + done;
        uint32_t block_idx = pos / block_size;          // логический блок внутри файла
        uint32_t offset_in_block = pos % block_size;    // смещение внутри блока

        uint32_t disk_block = inode_bmap(ino, block_idx);
        if (disk_block == 0) {
            // Если блок не выделен, считаем его заполненным нулями
            uint32_t chunk = block_size - offset_in_block;
            uint32_t remaining = ino->i_size - pos;
            if (chunk > remaining) chunk = remaining;
            if (chunk > len - done) chunk = len - done;
            memset(buf + done, 0, chunk);
            done += chunk;
            continue;
        }

        // Читаем весь блок в временный буфер
        if (read_one_block(disk_block, io_buf) != 0) {
            return -1;   // ошибка чтения с диска
        }

        uint32_t chunk = block_size - offset_in_block;
        uint32_t remaining = ino->i_size - pos;
        if (chunk > remaining) chunk = remaining;
        if (chunk > len - done) chunk = len - done;

        memcpy(buf + done, io_buf + offset_in_block, chunk);
        done += chunk;
    }
    return 0;
}

static int inode_write_range(uint32_t ino_num, struct ext2_inode *ino, uint32_t off, const uint8_t *buf, uint32_t len) {
    uint32_t end = off + len;
    if (end > ino->i_size) ino->i_size = end;
    uint32_t max_blocks = (ino->i_size + block_size - 1) / block_size;
    if (max_blocks > EXT2_NDIR_BLOCKS) {
        vga_write("ext2: file too large (only direct blocks)\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
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
    ino->i_blocks = (ino->i_size + block_size - 1) / block_size * (block_size / 512);
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

    // Всегда добавляем запись в конец директории
    uint32_t new_size = di.i_size + rl;
    if (inode_write_range(dir_ino, &di, di.i_size, ent, rl) != 0) return -1;
    // Обновляем размер директории
    di.i_size = new_size;
    if (write_inode_raw(dir_ino, &di) != 0) return -1;
    return 0;
}

static int dir_remove_entry(uint32_t dir_ino, const char *name) {
    struct ext2_inode di;
    if (read_inode_raw(dir_ino, &di) != 0) return -1;
    if ((di.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;

    uint32_t pos = 0;
    uint8_t *buf = dentry_read_buf; // размером block_size
    while (pos < di.i_size) {
        uint32_t block = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t db = inode_bmap(&di, block);
        if (!db) break;
        if (read_one_block(db, buf) != 0) break;

        uint32_t off = offset;
        while (off < block_size && pos + off < di.i_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(buf + off);
            uint16_t rec_len = de->rec_len;
            // Проверка валидности записи
            if (rec_len < 8 || off + rec_len > block_size) break;
            if (de->inode != 0 && de->name_len == strlen(name)) {
                char namebuf[256];
                memcpy(namebuf, de->name, de->name_len);
                namebuf[de->name_len] = '\0';
                if (strcmp(namebuf, name) == 0) {
                    // Помечаем запись как свободную (inode = 0)
                    de->inode = 0;
                    // Записываем изменённый блок обратно
                    if (write_one_block(db, buf) != 0) return -1;
                    // Обновляем inode каталога (размер не меняем, просто помечаем запись свободной)
                    // Обновляем время изменения
                    di.i_mtime = now_unix();
                    if (write_inode_raw(dir_ino, &di) != 0) return -1;
                    return 0;
                }
            }
            off += rec_len;
        }
        pos += (block_size - offset);
    }
    return -1; // запись не найдена
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

static void normalize_path(char *path) {
    char *p = path, *q = path;
    int last_was_slash = 0;
    while (*p) {
        if (*p == '/') {
            if (!last_was_slash) {
                *q++ = '/';
                last_was_slash = 1;
            }
        } else {
            *q++ = *p;
            last_was_slash = 0;
        }
        p++;
    }
    if (q > path && *(q-1) == '/') q--;
    *q = 0;
    if (*path == 0) {
        path[0] = '/';
        path[1] = 0;
    }
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

// ---------------------------------------------------------------------
// Поиск компонента в директории (возвращает inode)
static int dir_find_entry(uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    struct ext2_inode dir_inode;
    if (read_inode_raw(dir_ino, &dir_inode) != 0) return -1;
    if ((dir_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;

    uint32_t pos = 0;
    uint8_t *buf = dentry_read_buf; // размером block_size
    while (pos < dir_inode.i_size) {
        uint32_t block = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t db = inode_bmap(&dir_inode, block);
        if (!db) break;
        if (read_one_block(db, buf) != 0) break;

        uint32_t off = offset;
        while (off < block_size && pos + off < dir_inode.i_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(buf + off);
            uint16_t rec_len = de->rec_len;
            if (rec_len < 8 || off + rec_len > block_size) break;
            if (de->inode != 0 && de->name_len == strlen(name)) {
                char namebuf[256];
                memcpy(namebuf, de->name, de->name_len);
                namebuf[de->name_len] = '\0';
                if (strcmp(namebuf, name) == 0) {
                    *out_ino = de->inode;
                    return 0;
                }
            }
            off += rec_len;
        }
        pos += (block_size - offset);
    }
    return -1;
}

// ---------------------------------------------------------------------
// lookup_path — абсолютный путь (итеративный)
static int lookup_path(const char *path_in, uint32_t *out_ino, int expect_dir) {
    if (!fs_mounted) return -1;

    // Нормализация пути в абсолютный
    char abs_path[256];
    if (path_in[0] == '/') {
        strncpy(abs_path, path_in, sizeof(abs_path)-1);
        abs_path[sizeof(abs_path)-1] = 0;
    } else {
        strncpy(abs_path, cwd_path, sizeof(abs_path)-1);
        abs_path[sizeof(abs_path)-1] = 0;
        if (strcmp(cwd_path, "/") != 0)
            strncat(abs_path, "/", sizeof(abs_path)-strlen(abs_path)-1);
        strncat(abs_path, path_in, sizeof(abs_path)-strlen(abs_path)-1);
    }
    normalize_path(abs_path);
    vga_write("\n", VGA_COLOR_LIGHT_CYAN);

    // Корень
    if (strcmp(abs_path, "/") == 0) {
        *out_ino = EXT2_ROOT_INO;
        return 0;
    }

    uint32_t cur_ino = EXT2_ROOT_INO;
    char *p = abs_path;
    if (*p == '/') p++;
    char *next;

    while (p && *p) {
        next = strchr(p, '/');
        char comp[128];
        if (next) {
            size_t len = next - p;
            if (len >= sizeof(comp)) return -1;
            memcpy(comp, p, len);
            comp[len] = '\0';
            p = next + 1;
        } else {
            strncpy(comp, p, sizeof(comp)-1);
            comp[sizeof(comp)-1] = '\0';
            p = NULL;
        }

        uint32_t next_ino;
        if (dir_find_entry(cur_ino, comp, &next_ino) != 0)
            return -1; // компонент не найден

        // Если это не последний компонент, проверяем, что это директория
        if (p && *p) {
            struct ext2_inode tmp;
            if (read_inode_raw(next_ino, &tmp) != 0) return -1;
            if ((tmp.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
        }
        cur_ino = next_ino;
    }

    if (expect_dir) {
        struct ext2_inode fin;
        if (read_inode_raw(cur_ino, &fin) != 0) return -1;
        if ((fin.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
    }
    *out_ino = cur_ino;
    return 0;
}

static int load_super_and_groups(void) {
    uint32_t sector = sect_from_block(1);
    char buf[16];
    vga_write("[ext2] reading superblock at sector ", VGA_COLOR_LIGHT_CYAN);
    dbstring(&DEBUG, "[ext2]: reading superblock at sector:");
    int_to_str(sector, buf);
    vga_write(buf, VGA_COLOR_LIGHT_CYAN);
    dbstring(&INFO, buf);
    vga_write("\n", VGA_COLOR_LIGHT_CYAN);

    // Читаем суперблок (блок 1)
    if (read_one_block(1, io_buf) != 0) {
        vga_write("[ext2] ERROR: read_one_block failed\n", VGA_COLOR_LIGHT_RED);
        dbstring(&ERR, "[ext2]: read_one_block failed");
        return -1;
    }
    memcpy(&sb, io_buf, sizeof(sb));

    // Проверяем магическое число
    if (sb.s_magic != EXT2_SUPER_MAGIC) {
        vga_write("[ext2] ERROR: bad magic number\n", VGA_COLOR_LIGHT_RED);
        dbstring(&ERR, "[ext2]: bad magic number.");
        return -1;
    }
    vga_write("[ext2] superblock magic OK\n", VGA_COLOR_LIGHT_GREEN);
    dbstring(&DEBUG, "[ext2]: superblock magic: OK.");

    // Вычисляем размер блока
    block_size = 1024u << sb.s_log_block_size;
    if (block_size == 0 || block_size > 4096) return -1;
    vga_write("[ext2] block size: ", VGA_COLOR_LIGHT_CYAN);
    int_to_str(block_size, buf);
    vga_write(buf, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_CYAN);

    ptrs_per_block = block_size / 4;   // количество указателей в блоке косвенной адресации
    if (sb.s_blocks_per_group == 0) return -1;
    num_groups = (sb.s_blocks_count + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    if (num_groups == 0 || num_groups > MAX_GROUPS) return -1;
    vga_write("[ext2] groups: ", VGA_COLOR_LIGHT_CYAN);
    int_to_str(num_groups, buf);
    vga_write(buf, VGA_COLOR_LIGHT_CYAN);
    vga_write("\n", VGA_COLOR_LIGHT_CYAN);

    // Чтение таблицы групп дескрипторов (GDT)
    uint32_t total = num_groups * (uint32_t)sizeof(groups[0]);
    uint32_t pos = 0;
    while (pos < total) {
        uint32_t blk = 2 + pos / block_size;      // GDT начинается с блока 2
        uint32_t off = pos % block_size;
        if (read_one_block(blk, io_buf) != 0) return -1;
        uint32_t chunk = block_size - off;
        if (chunk > total - pos) chunk = total - pos;
        memcpy((uint8_t *)groups + pos, io_buf + off, chunk);
        if (num_groups > 0 && pos == 0) {
            vga_write("[ext2] group0: bb=", VGA_COLOR_LIGHT_CYAN);
            int_to_str(groups[0].bg_block_bitmap, buf);
            vga_write(buf, VGA_COLOR_LIGHT_CYAN);
            vga_write(" ib=", VGA_COLOR_LIGHT_CYAN);
            int_to_str(groups[0].bg_inode_bitmap, buf);
            vga_write(buf, VGA_COLOR_LIGHT_CYAN);
            vga_write(" it=", VGA_COLOR_LIGHT_CYAN);
            int_to_str(groups[0].bg_inode_table, buf);
            vga_write(buf, VGA_COLOR_LIGHT_CYAN);
            vga_write("\n", VGA_COLOR_LIGHT_CYAN);
        }
        pos += chunk;
    }
    vga_write("[ext2] superblock and GDT loaded successfully\n", VGA_COLOR_LIGHT_GREEN);
    dbstring(&DEBUG, "[ext2]: superblock and GDT loaded successfully");
    return 0;
}

static int ext2_format_partition(uint32_t part_sectors) {
    uint32_t total_blocks = (part_sectors * 512) / 1024;
    if (total_blocks < 24) return -1;

    const uint32_t ipg = 128;          // inodes per group
    uint32_t bpbg = 8192;              // blocks per group
    if (total_blocks < bpbg) bpbg = total_blocks;
    uint32_t ng = (total_blocks + bpbg - 1) / bpbg;
    if (ng == 0 || ng > MAX_GROUPS) return -1;

    uint32_t itb = (ipg * EXT2_GOOD_OLD_INODE_SIZE + 1023) / 1024; // inode table blocks
    uint32_t gdt_blocks = (ng * 32u + 1023) / 1024;

    uint32_t bb0 = 2 + gdt_blocks;        // block bitmap for group0
    uint32_t ib0 = bb0 + 1;               // inode bitmap for group0
    uint32_t it0 = ib0 + 1;               // inode table for group0
    uint32_t first_data = it0 + itb;      // first data block

    // Заполняем суперблок
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
    if (partition_offset == 0)
        sb.s_first_data_block = 1;      // whole-disk
    else
        sb.s_first_data_block = first_data;

    // Инициализируем группы
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

    // Записываем суперблок (блок 1)
    memset(io_buf, 0, 1024);
    memcpy(io_buf, &sb, sizeof(sb));
    if (write_one_block(1, io_buf) != 0) return -1;

    // Записываем GDT (блоки 2 .. 2+gdt_blocks-1)
    uint32_t total_gdt = ng * (uint32_t)sizeof(groups[0]);
    uint32_t pos = 0;
    while (pos < total_gdt) {
        uint32_t blk = 2 + pos / 1024;
        uint32_t off = pos % 1024;
        if (read_one_block(blk, io_buf) != 0) return -1;
        uint32_t chunk = 1024 - off;
        if (chunk > total_gdt - pos) chunk = total_gdt - pos;
        memcpy(io_buf + off, (uint8_t *)groups + pos, chunk);
        if (write_one_block(blk, io_buf) != 0) return -1;
        pos += chunk;
    }

    // Инициализируем битовые карты блоков для всех групп
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t bb = groups[g].bg_block_bitmap;
        uint32_t gs = g * bpbg;
        memset(io_buf, 0, 1024);
        // Помечаем занятые блоки: суперблок, GDT, битовые карты, таблицы inode
        for (uint32_t b = 0; b < bpbg && gs + b < total_blocks; b++) {
            uint32_t abs_b = gs + b;
            // Блок занят, если он входит в метаданные группы
            int used = 0;
            if (g == 0) {
                if (abs_b < first_data) used = 1;
            } else {
                if (abs_b == groups[g].bg_block_bitmap ||
                    abs_b == groups[g].bg_inode_bitmap ||
                    (abs_b >= groups[g].bg_inode_table && abs_b < groups[g].bg_inode_table + itb))
                    used = 1;
            }
            if (used) set_bmap_bit(io_buf, b);
        }
        if (write_one_block(bb, io_buf) != 0) return -1;
    }

    // Инициализируем битовые карты inode для всех групп
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t ib = groups[g].bg_inode_bitmap;
        memset(io_buf, 0, 1024);
        uint32_t first_ino = (g == 0) ? 11 : 1;
        for (uint32_t i = 0; i < first_ino && i < ipg; i++)
            set_bmap_bit(io_buf, i);
        if (write_one_block(ib, io_buf) != 0) return -1;
    }

    // Обнуляем таблицы inode для всех групп (только метаданные, не все блоки данных)
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t it = groups[g].bg_inode_table;
        for (uint32_t b = 0; b < itb; b++) {
            memset(io_buf, 0, 1024);
            if (write_one_block(it + b, io_buf) != 0) return -1;
        }
    }

    // Создаём корневой каталог (inode 2)
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

    // Записываем содержимое корневого каталога (записи "." и "..")
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
    if (write_one_block(root_data, io_buf) != 0) return -1;

    // Обновляем счётчик использованных директорий в группе 0
    groups[0].bg_used_dirs_count = 1;
    // Перезаписываем GDT
    pos = 0;
    while (pos < total_gdt) {
        uint32_t blk = 2 + pos / 1024;
        uint32_t off = pos % 1024;
        if (read_one_block(blk, io_buf) != 0) return -1;
        uint32_t chunk = 1024 - off;
        if (chunk > total_gdt - pos) chunk = total_gdt - pos;
        memcpy(io_buf + off, (uint8_t *)groups + pos, chunk);
        if (write_one_block(blk, io_buf) != 0) return -1;
        pos += chunk;
    }

    // Перезаписываем суперблок (обновлённое поле s_free_blocks_count и т.д.)
    memset(io_buf, 0, 1024);
    memcpy(io_buf, &sb, sizeof(sb));
    if (write_one_block(1, io_buf) != 0) return -1;

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
    dbstring(&DEBUG, "[ext2]: succesfully installed.");
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

int fs_load_from_disk(void) {
    // Проверяем whole-disk (без MBR)
    partition_offset = 0;
    if (load_super_and_groups() == 0) {
        fs_mounted = 1;
        vga_write("[ext2] mounted whole-disk (offset 0)\n", VGA_COLOR_LIGHT_GREEN);
        return 0;
    }
    // Проверяем первый раздел (MBR, LBA=1)
    partition_offset = 1;
    if (load_super_and_groups() == 0) {
        fs_mounted = 1;
        vga_write("[ext2] mounted first partition (offset 1)\n", VGA_COLOR_LIGHT_GREEN);
        return 0;
    }
    // Ничего не найдено – создаём новую ФС
    vga_write("[ext2] no valid filesystem found, creating...\n", VGA_COLOR_LIGHT_YELLOW);
    // Пробуем форматировать whole-disk
    partition_offset = 0;
    if (fs_format_partition(disk_total_sectors) == 0) {
        fs_mounted = 1;
        vga_write("[ext2] created whole-disk ext2 (offset 0)\n", VGA_COLOR_LIGHT_GREEN);
        return 0;
    }
    // Если whole-disk не получился, пробуем MBR+раздел
    partition_offset = 1;
    if (fs_format_partition(disk_total_sectors - 1) == 0) {
        fs_mounted = 1;
        vga_write("[ext2] created MBR+partition ext2 (offset 1)\n", VGA_COLOR_LIGHT_GREEN);
        return 0;
    }
    // Полная неудача
    vga_write("[ext2] FATAL: cannot create filesystem\n", VGA_COLOR_LIGHT_RED);
    partition_offset = 0;
    fs_mounted = 0;
    return -1;
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
        if (rl < 8 || pos + rl > ri.i_size) break;
        if (inode_read_range(&ri, pos, dentry_read_buf, rl) != 0) break;
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
        if (de->rec_len != rl && de->rec_len < rl) break;
        char nb[256];
        if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
        memcpy(nb, de->name, de->name_len);
        nb[de->name_len] = 0;
        if (de->inode != 0 && strcmp(nb, ".") != 0 && strcmp(nb, "..") != 0) n++;
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
            if (rl < 8 || pos + rl > target.i_size) break;
            if (inode_read_range(&target, pos, dentry_read_buf, rl) != 0) break;
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
            if (de->rec_len != rl && de->rec_len < rl) break;
            char nb[256];
            if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
            memcpy(nb, de->name, de->name_len);
            nb[de->name_len] = 0;
            if (de->inode != 0 && strcmp(nb, ".") != 0 && strcmp(nb, "..") != 0) cnt++;
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
    int ret = lookup_path(abs, &ino, 0);
    if (ret != 0) {
        char buf[8];
        int_to_str(ret, buf);
        vga_write(buf, VGA_COLOR_LIGHT_RED);
        vga_write("\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) {
        return -1;
    }
    if ((in.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
        vga_write("It's directory", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    uint32_t sz = in.i_size;
    if (sz > buf_max) sz = buf_max;
    if (inode_read_range(&in, 0, data, sz) != 0) {
        return -1;
    }
    *size_out = sz;
    char buf[16];
    int_to_str(sz, buf);
    return 0;
}

int fs_write(const char *name, const uint8_t *data, uint32_t size) {
    if (!fs_mounted) return -1;
    char abs[256];
    uint32_t ino;
    path_to_abs(name, abs, sizeof(abs));
    if (lookup_path(abs, &ino, 0) != 0) {
        // Файл не существует, создаём
        if (fs_create(abs, 0) != 0) return -1;
        if (lookup_path(abs, &ino, 0) != 0) return -1;
    }
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) return -1;
    if ((in.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) return -1;
    // Усекаем файл до 0
    truncate_inode_blocks(&in);
    // Записываем новые данные
    if (size == 0) {
        write_inode_raw(ino, &in);
        fs_sync_to_disk();
        return 0;
    }
    // Временно поддерживаем только прямые блоки (размер <= 12*block_size)
    if (size > 12 * block_size) {
        vga_write("fs_write: file too large (only direct blocks)\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }
    // Используем inode_write_range (которая должна поддерживать прямые блоки)
    if (inode_write_range(ino, &in, 0, data, size) != 0) return -1;
    fs_sync_to_disk();
    return 0;
}

void fs_list(void) {
    if (!fs_mounted) return;
    struct ext2_inode di;
    if (read_inode_raw(cwd_ino, &di) != 0) return;
    uint32_t pos = 0;
    while (pos < di.i_size) {
        uint8_t hdr[8];
        if (inode_read_range(&di, pos, hdr, 8) != 0) break;
        uint16_t rl = (uint16_t)(hdr[4] | (hdr[5] << 8));
        if (rl < 8 || pos + rl > di.i_size) break;
        if (inode_read_range(&di, pos, dentry_read_buf, rl) != 0) break;
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dentry_read_buf;
        if (de->rec_len != rl && de->rec_len < rl) break;
        char nb[256];
        if ((size_t)de->name_len + 1 >= sizeof(nb)) break;
        memcpy(nb, de->name, de->name_len);
        nb[de->name_len] = 0;
        if (de->inode != 0 && strcmp(nb, ".") != 0 && strcmp(nb, "..") != 0) {
            vga_write(nb, VGA_COLOR_LIGHT_GREY);
            if (de->file_type == EXT2_FT_DIR) vga_write("/", VGA_COLOR_LIGHT_CYAN);
            vga_write("  ", VGA_COLOR_LIGHT_GREY);
        }
        pos += rl;
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

int fs_change_dir(const char *path) { return fs_cd(path); }

int fs_format_partition(uint32_t part_sectors) {
    return ext2_format_partition(part_sectors);
}
static const char *my_strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return last;
}
// Перемещение/переименование в пределах одной файловой системы
int fs_rename(const char *oldpath, const char *newpath) {
    if (!fs_mounted) return -1;
    
    // 1. Получаем inode старого пути
    uint32_t old_ino;
    if (lookup_path(oldpath, &old_ino, 0) != 0) return -1;
    
    // 2. Если новый путь существует и является файлом (не директорией), удаляем его
    if (fs_exists(newpath) && !fs_is_directory(newpath)) {
        if (fs_delete(newpath) != 0) return -1;
    }
    
    // 3. Если новый путь – директория, то перемещаем внутрь, сохраняя базовое имя
    if (fs_is_directory(newpath)) {
        // Находим базовое имя oldpath
        const char *base = oldpath;
        for (const char *p = oldpath; *p; p++) if (*p == '/') base = p + 1;
        char real_new[512];
        strcpy(real_new, newpath);
        strcat(real_new, "/");
        strcat(real_new, base);
        return fs_rename(oldpath, real_new);
    }
    
    // 4. Разбираем новый путь на родительскую директорию и имя
    char parent[256], name[128];
    if (split_parent_name(newpath, parent, name) != 0) return -1;
    uint32_t p_ino;
    if (lookup_path(parent, &p_ino, 1) != 0) return -1;
    
    // 5. Удаляем старую запись из родительского каталога старого пути
    char old_parent[256], old_name[128];
    if (split_parent_name(oldpath, old_parent, old_name) != 0) return -1;
    uint32_t old_p_ino;
    if (lookup_path(old_parent, &old_p_ino, 1) != 0) return -1;
    if (dir_remove_entry(old_p_ino, old_name) != 0) return -1;
    
    // 6. Определяем тип файла (директория или обычный)
    struct ext2_inode inode;
    if (read_inode_raw(old_ino, &inode) != 0) return -1;
    uint8_t type = ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    
    // 7. Добавляем новую запись в новый родительский каталог
    if (dir_add_entry(p_ino, name, old_ino, type) != 0) {
        // Откат: восстановить старую запись (упрощённо – просто вернуть ошибку)
        return -1;
    }
    
    // 8. Если перемещается директория, обновляем счётчики ссылок (опционально, для целостности)
    if (type == EXT2_FT_DIR) {
        // Уменьшаем счётчик ссылок в старом родителе (было +1 за запись "..")
        struct ext2_inode old_p_inode;
        if (read_inode_raw(old_p_ino, &old_p_inode) == 0 && old_p_inode.i_links_count > 0) {
            old_p_inode.i_links_count--;
            write_inode_raw(old_p_ino, &old_p_inode);
        }
        // Увеличиваем счётчик ссылок в новом родителе
        struct ext2_inode p_inode;
        if (read_inode_raw(p_ino, &p_inode) == 0) {
            p_inode.i_links_count++;
            write_inode_raw(p_ino, &p_inode);
        }
    }
    
    fs_sync_to_disk();
    return 0;
}

int fs_mv_path(const char *from, const char *to) {
    return fs_rename(from, to);
}
void fs_list_at_path(const char *ext2_abs_dir) {
    if (!fs_mounted) {
        vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t ino;
    if (lookup_path(ext2_abs_dir, &ino, 1) != 0) {
        vga_write("ls: bad path\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    struct ext2_inode di;
    if (read_inode_raw(ino, &di) != 0) {
        vga_write("ls: cannot read inode\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if ((di.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        vga_write("ls: not a directory\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t pos = 0;
    while (pos < di.i_size) {
        uint32_t block_idx = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t disk_block = inode_bmap(&di, block_idx);
        if (disk_block == 0) break;
        uint8_t buf[block_size];
        if (read_one_block(disk_block, buf) != 0) break;
        uint32_t off = offset;
        while (off < block_size && pos + off < di.i_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(buf + off);
            uint16_t rec_len = de->rec_len;
            if (rec_len < 8 || off + rec_len > block_size) break;
            if (de->inode != 0 && de->name_len > 0 && de->name_len <= 255) {
                char name[256];
                memcpy(name, de->name, de->name_len);
                name[de->name_len] = '\0';
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                    vga_write(name, VGA_COLOR_LIGHT_GREY);
                    if (de->file_type == EXT2_FT_DIR)
                        vga_write("/", VGA_COLOR_LIGHT_CYAN);
                    vga_write("  ", VGA_COLOR_LIGHT_GREY);
                }
            }
            off += rec_len;
        }
        pos += block_size - offset;
    }
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
}

void fs_list_long_at_path(const char *ext2_abs_dir, int show_all) {
    if (!fs_mounted) {
        vga_write("(ext2 not mounted)\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t ino;
    if (lookup_path(ext2_abs_dir, &ino, 1) != 0) {
        vga_write("ls: bad path\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    struct ext2_inode di;
    if (read_inode_raw(ino, &di) != 0) {
        vga_write("ls: cannot read inode\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if ((di.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        vga_write("ls: not a directory\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t pos = 0;
    while (pos < di.i_size) {
        uint32_t block_idx = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t disk_block = inode_bmap(&di, block_idx);
        if (disk_block == 0) break;
        uint8_t buf[block_size];
        if (read_one_block(disk_block, buf) != 0) break;
        uint32_t off = offset;
        while (off < block_size && pos + off < di.i_size) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(buf + off);
            uint16_t rec_len = de->rec_len;
            if (rec_len < 8 || off + rec_len > block_size) break;
            if (de->inode != 0 && de->name_len > 0 && de->name_len <= 255) {
                char name[256];
                memcpy(name, de->name, de->name_len);
                name[de->name_len] = '\0';
                if (!show_all && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) {
                    off += rec_len;
                    continue;
                }
                struct ext2_inode fin;
                if (read_inode_raw(de->inode, &fin) != 0) {
                    off += rec_len;
                    continue;
                }
                char md[12];
                md[0] = ((fin.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) ? 'd' : '-';
                uint16_t perm = fin.i_mode & 0777;
                const char *rwx = "rwxrwxrwx";
                for (int j = 0; j < 9; j++)
                    md[1+j] = (perm & (1u << (8-j))) ? rwx[j] : '-';
                md[10] = 0;
                vga_write(md, VGA_COLOR_LIGHT_GREY);
                vga_write(" 1 root root ", VGA_COLOR_LIGHT_GREY);
                char szb[16];
                int_to_str((int)fin.i_size, szb);
                vga_write(szb, VGA_COLOR_LIGHT_CYAN);
                vga_write(" 0 ", VGA_COLOR_LIGHT_GREY);
                vga_write(name, VGA_COLOR_LIGHT_GREEN);
                if (de->file_type == EXT2_FT_DIR)
                    vga_write("/", VGA_COLOR_LIGHT_CYAN);
                vga_write("\n", VGA_COLOR_LIGHT_GREY);
            }
            off += rec_len;
        }
        pos += block_size - offset;
    }
}

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
    if (read_inode_raw(ino, &in) != 0) {
        vga_write("Cannot read inode\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    if ((in.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
        vga_write("Is a directory\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    uint32_t sz = in.i_size;
    uint8_t *buf = (uint8_t*)0x300000; // временный буфер в безопасной области (3 MiB)
    uint32_t pos = 0;
    while (pos < sz) {
        uint32_t chunk = sz - pos;
        if (chunk > 4096) chunk = 4096;
        if (inode_read_range(&in, pos, buf, chunk) != 0) {
            vga_write("\n[Read error]\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        // Выводим накопленный блок целиком
        buf[chunk] = '\0';
        vga_write((char*)buf, VGA_COLOR_LIGHT_GREY);
        pos += chunk;
    }
    vga_putchar('\n', VGA_COLOR_LIGHT_GREY);
}

void cmd_ynan_at(const char *ext2_abs_path) {
    uint16_t saved_screen[VGA_WIDTH * VGA_HEIGHT];
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        saved_screen[i] = VGA_MEMORY[i];
    uint8_t saved_cx, saved_cy;
    vga_get_cursor(&saved_cx, &saved_cy);
    vga_clear(VGA_COLOR_BLACK | (VGA_COLOR_BLACK << 4));
    vga_set_cursor(0, 0);
    vga_write("Editing: ", VGA_COLOR_LIGHT_GREY);
    vga_write(ext2_abs_path, VGA_COLOR_LIGHT_BLUE);
    vga_write("\n", VGA_COLOR_LIGHT_GREY);
    vga_write("Arrow keys to move, ESC to save, '.' + Enter on empty line to save\n", VGA_COLOR_LIGHT_CYAN);
    vga_write("------------------------------------------------------------\n", VGA_COLOR_LIGHT_GREY);
#define MAX_EDIT_SIZE 8192
    char *buffer = (char *)(uintptr_t)0x200000;
    int size = 0, cursor = 0;
    uint32_t loaded_size = 0;
    if (fs_open(ext2_abs_path, (uint8_t *)buffer, MAX_EDIT_SIZE - 1, &loaded_size) == 0) {
        size = (int)loaded_size;
        buffer[size] = '\0';
        cursor = size;
    } else {
        buffer[0] = '\0';
        size = 0;
        cursor = 0;
    }
    int cursor_x = 0, cursor_y = 2;
    // Локальная функция editor_draw
    void editor_draw(const char *buf, int cpos, int start_row, int *out_x, int *out_y) {
        int row = start_row;
        int col = 0;
        int current_pos = 0;
        int found = 0;
        for (int r = start_row; r <= VGA_LAST_TEXT_ROW; r++)
            for (int c = 0; c < VGA_WIDTH; c++)
                VGA_MEMORY[r * VGA_WIDTH + c] = (VGA_COLOR_LIGHT_GREY << 8) | ' ';
        for (int i = 0; buf[i] && row <= VGA_LAST_TEXT_ROW; i++) {
            if (buf[i] == '\n') {
                row++;
                col = 0;
                current_pos++;
                continue;
            }
            if (row <= VGA_LAST_TEXT_ROW && col < VGA_WIDTH) {
                VGA_MEMORY[row * VGA_WIDTH + col] = (VGA_COLOR_LIGHT_GREY << 8) | buf[i];
                if (current_pos == cpos) {
                    *out_x = col;
                    *out_y = row;
                    found = 1;
                }
                col++;
            }
            current_pos++;
        }
        if (!found) {
            int r = start_row, c = 0, pos = 0;
            for (int i = 0; buf[i] && pos <= cpos; i++) {
                if (buf[i] == '\n') {
                    r++;
                    c = 0;
                } else if (pos == cpos)
                    break;
                else
                    c++;
                pos++;
            }
            if (r > VGA_LAST_TEXT_ROW) r = VGA_LAST_TEXT_ROW;
            if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
            *out_x = c;
            *out_y = r;
        }
    }
    editor_draw(buffer, cursor, 2, &cursor_x, &cursor_y);
    vga_set_cursor(cursor_x, cursor_y);
    int running = 1;  // <-- ОБЪЯВЛЕНИЕ ПЕРЕМЕННОЙ
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
            if (fs_write(ext2_abs_path, (uint8_t *)buffer, (uint32_t)size) == 0)
                vga_write("Saved.\n", VGA_COLOR_LIGHT_GREEN);
            else
                vga_write("Failed to save file.\n", VGA_COLOR_LIGHT_RED);
        } else {
            fs_delete(ext2_abs_path);
            vga_write("Empty file deleted.\n", VGA_COLOR_LIGHT_GREEN);
        }
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = saved_screen[i];
    vga_set_cursor(saved_cx, saved_cy);
    vga_taskbar_refresh();
}
// Рекурсивное удаление дерева каталогов (rm -rf)
int fs_rm_rf(const char *path) {
    if (!fs_mounted) return -1;
    uint32_t ino;
    if (lookup_path(path, &ino, 0) != 0) return -1;
    struct ext2_inode in;
    if (read_inode_raw(ino, &in) != 0) return -1;
    
    // Если это не директория, просто удаляем
    if ((in.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return fs_delete(path);
    }
    
    // Это директория: обходим содержимое
    uint32_t pos = 0;
    uint8_t *buf = (uint8_t*)0x300000; // временный буфер
    char fullpath[256];
    while (pos < in.i_size) {
        uint32_t chunk = in.i_size - pos;
        if (chunk > 512) chunk = 512;
        if (inode_read_range(&in, pos, buf, chunk) != 0) break;
        uint32_t off = 0;
        while (off < chunk) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2*)(buf + off);
            uint16_t rec_len = de->rec_len;
            if (rec_len < 8 || off + rec_len > chunk) break;
            if (de->inode != 0) {
                char name[256];
                memcpy(name, de->name, de->name_len);
                name[de->name_len] = '\0';
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                    strcpy(fullpath, path);
                    strcat(fullpath, "/");
                    strcat(fullpath, name);
                    if (fs_rm_rf(fullpath) != 0) return -1;
                }
            }
            off += rec_len;
        }
        pos += chunk;
    }
    // Удаляем саму директорию
    return fs_delete(path);
}
int fs_readdir(const char *path, fs_dirent_t *entries, int max_entries) {
    if (!fs_mounted) return -1;
    uint32_t ino;
    if (lookup_path(path, &ino, 1) != 0) return -1;
    struct ext2_inode di;
    if (read_inode_raw(ino, &di) != 0) return -1;
    if ((di.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;

    int count = 0;
    uint32_t pos = 0;
    static uint8_t buf[4096];  // локальный буфер, но лучше использовать глобальный dentry_read_buf
    // Если у вас есть глобальный dentry_read_buf, используйте его:
    // uint8_t *buf = dentry_read_buf;

    while (pos < di.i_size && count < max_entries) {
        uint32_t block = pos / block_size;
        uint32_t offset = pos % block_size;
        uint32_t db = inode_bmap(&di, block);
        if (db == 0) break;
        if (read_one_block(db, buf) != 0) break;

        uint32_t off = offset;
        while (off < block_size && pos + off < di.i_size && count < max_entries) {
            struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(buf + off);
            uint16_t rec_len = de->rec_len;
            if (rec_len < 8 || off + rec_len > block_size) break;

            if (de->inode != 0 && de->name_len > 0 && de->name_len <= 255) {
                // Копируем имя
                if (de->name_len >= sizeof(entries[count].name)) {
                    off += rec_len;
                    continue;
                }
                memcpy(entries[count].name, de->name, de->name_len);
                entries[count].name[de->name_len] = '\0';
                entries[count].ino = de->inode;
                entries[count].type = (de->file_type == EXT2_FT_DIR) ? 2 : 1;

                // Пропускаем "." и ".."
                if (strcmp(entries[count].name, ".") != 0 && strcmp(entries[count].name, "..") != 0) {
                    count++;
                }
            }
            off += rec_len;
        }
        pos += (block_size - offset);
    }
    return count;
}
