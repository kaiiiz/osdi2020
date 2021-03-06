#include "fat32.h"

#include "mm.h"
#include "my_string.h"
#include "sdhost.h"
#include "uart0.h"
#include "vfs.h"

struct fat32_metadata fat32_metadata;

struct vnode_operations* fat32_v_ops = NULL;
struct file_operations* fat32_f_ops = NULL;

static uint32_t get_cluster_blk_idx(uint32_t cluster_idx) {
    return fat32_metadata.data_region_blk_idx +
           (cluster_idx - fat32_metadata.first_cluster) * fat32_metadata.sector_per_cluster;
}

static uint32_t get_fat_blk_idx(uint32_t cluster_idx) {
    return fat32_metadata.fat_region_blk_idx + (cluster_idx / FAT_ENTRY_PER_BLOCK);
}

struct vnode* fat32_create_vnode(struct dentry* dentry) {
    struct vnode* vnode = (struct vnode*)kmalloc(sizeof(struct vnode));
    vnode->dentry = dentry;
    vnode->f_ops = fat32_f_ops;
    vnode->v_ops = fat32_v_ops;
    return vnode;
}

struct dentry* fat32_create_dentry(struct dentry* parent, const char* name, int type) {
    struct dentry* dentry = (struct dentry*)kmalloc(sizeof(struct dentry));
    strcpy(dentry->name, name);
    dentry->parent = parent;
    list_head_init(&dentry->list);
    list_head_init(&dentry->childs);
    if (parent != NULL) {
        list_add(&dentry->list, &parent->childs);
    }
    dentry->vnode = fat32_create_vnode(dentry);
    dentry->mountpoint = NULL;
    dentry->type = type;
    return dentry;
}

// error code: -1: already register
int fat32_register() {
    if (fat32_v_ops != NULL && fat32_f_ops != NULL) {
        return -1;
    }
    fat32_v_ops = (struct vnode_operations*)kmalloc(sizeof(struct vnode_operations));
    fat32_v_ops->create = fat32_create;
    fat32_v_ops->lookup = fat32_lookup;
    fat32_v_ops->ls = fat32_ls;
    fat32_v_ops->mkdir = fat32_mkdir;
    fat32_v_ops->load_dentry = fat32_load_dentry;
    fat32_f_ops = (struct file_operations*)kmalloc(sizeof(struct file_operations));
    fat32_f_ops->read = fat32_read;
    fat32_f_ops->write = fat32_write;
    return 0;
}

int fat32_setup_mount(struct filesystem* fs, struct mount* mount) {
    mount->fs = fs;
    mount->root = fat32_create_dentry(NULL, "/", DIRECTORY);
    return 0;
}

int fat32_lookup(struct vnode* dir, struct vnode** target, const char* component_name) {
    // component_name is empty, return dir vnode
    if (!strcmp(component_name, "")) {
        *target = dir;
        return 0;
    }
    // search component_name in dir
    struct list_head* p = &dir->dentry->childs;
    list_for_each(p, &dir->dentry->childs) {
        struct dentry* dentry = list_entry(p, struct dentry, list);
        if (!strcmp(dentry->name, component_name)) {
            *target = dentry->vnode;
            return 0;
        }
    }
    *target = NULL;
    return -1;
}

int fat32_create(struct vnode* dir, struct vnode** target, const char* component_name) {
    return 0;
}

int fat32_ls(struct vnode* dir) {
}

int fat32_mkdir(struct vnode* dir, struct vnode** target, const char* component_name) {
}

int fat32_load_dentry(struct dentry* dir, char* component_name) {
    // read first block of cluster
    struct fat32_internal* dir_internal = (struct fat32_internal*)dir->vnode->internal;
    uint8_t sector[BLOCK_SIZE];
    uint32_t dirent_cluster = get_cluster_blk_idx(dir_internal->first_cluster);
    readblock(dirent_cluster, sector);

    // parse
    struct fat32_dirent* sector_dirent = (struct fat32_dirent*)sector;

    // load all children under dentry
    int found = -1;
    for (int i = 0; sector_dirent[i].name[0] != '\0'; i++) {
        // special value
        if (sector_dirent[i].name[0] == 0xE5) {
            continue;
        }
        // get filename
        char filename[13];
        int len = 0;
        for (int j = 0; j < 8; j++) {
            char c = sector_dirent[i].name[j];
            if (c == ' ') {
                break;
            }
            filename[len++] = c;
        }
        filename[len++] = '.';
        for (int j = 0; j < 3; j++) {
            char c = sector_dirent[i].ext[j];
            if (c == ' ') {
                break;
            }
            filename[len++] = c;
        }
        filename[len++] = 0;
        if (!strcmp(filename, component_name)) {
            found = 0;
        }
        // create dirent
        struct dentry* dentry;
        if (sector_dirent[i].attr == 0x10) {  // directory
            dentry = fat32_create_dentry(dir, filename, DIRECTORY);
        }
        else {  // file
            dentry = fat32_create_dentry(dir, filename, REGULAR_FILE);
        }
        // create fat32 internal
        struct fat32_internal* child_internal = (struct fat32_internal*)kmalloc(sizeof(struct fat32_internal));
        child_internal->first_cluster = ((sector_dirent[i].cluster_high) << 16) | (sector_dirent[i].cluster_low);
        child_internal->dirent_cluster = dirent_cluster;
        child_internal->size = sector_dirent[i].size;
        dentry->vnode->internal = child_internal;
    }
    return found;
}

// file operations
int fat32_read(struct file* file, void* buf, uint64_t len) {
    struct fat32_internal* file_node = (struct fat32_internal*)file->vnode->internal;
    uint64_t f_pos_ori = file->f_pos;
    uint32_t current_cluster = file_node->first_cluster;
    int remain_len = len;
    int fat[FAT_ENTRY_PER_BLOCK];

    while (remain_len > 0 && current_cluster >= fat32_metadata.first_cluster && current_cluster != EOC) {
        readblock(get_cluster_blk_idx(current_cluster), buf + file->f_pos); // NEED FIX: buf
        file->f_pos += (remain_len < BLOCK_SIZE) ? remain_len : BLOCK_SIZE;
        remain_len -= BLOCK_SIZE;

        // update cluster number from FAT
        if (remain_len > 0) {
            readblock(get_fat_blk_idx(current_cluster), fat);
            current_cluster = fat[current_cluster % FAT_ENTRY_PER_BLOCK];
        }
    }

    return file->f_pos - f_pos_ori;
}

int fat32_write(struct file* file, const void* buf, uint64_t len) {
    struct fat32_internal* file_node = (struct fat32_internal*)file->vnode->internal;
    uint64_t f_pos_ori = file->f_pos;
    int fat[FAT_ENTRY_PER_BLOCK];
    char write_buf[BLOCK_SIZE];

    // traversal to target cluster using f_pos
    uint32_t current_cluster = file_node->first_cluster;
    int remain_offset = file->f_pos;
    while (remain_offset > 0 && current_cluster >= fat32_metadata.first_cluster && current_cluster != EOC) {
        remain_offset -= BLOCK_SIZE;
        if (remain_offset > 0) {
            readblock(get_fat_blk_idx(current_cluster), fat);
            current_cluster = fat[current_cluster % FAT_ENTRY_PER_BLOCK];
        }
    }

    // write first block, handle f_pos
    int buf_idx, f_pos_offset = file->f_pos % BLOCK_SIZE;
    readblock(get_cluster_blk_idx(current_cluster), write_buf);
    for (buf_idx = 0; buf_idx < BLOCK_SIZE - f_pos_offset && buf_idx < len; buf_idx++) {
        write_buf[buf_idx + f_pos_offset] = ((char*)buf)[buf_idx];
    }
    writeblock(get_cluster_blk_idx(current_cluster), write_buf);
    file->f_pos += buf_idx;

    // write complete block
    int remain_len = len - buf_idx;
    while (remain_len > 0 && current_cluster >= fat32_metadata.first_cluster && current_cluster != EOC) {
        // write block
        writeblock(get_cluster_blk_idx(current_cluster), buf + buf_idx);
        file->f_pos += (remain_len < BLOCK_SIZE) ? remain_len : BLOCK_SIZE;
        remain_len -= BLOCK_SIZE;
        buf_idx += BLOCK_SIZE;

        // update cluster number from FAT
        if (remain_len > 0) {
            readblock(get_fat_blk_idx(current_cluster), fat);
            current_cluster = fat[current_cluster % FAT_ENTRY_PER_BLOCK];
        }
    }

    // TODO: last block also need to handle remainder

    // update file size
    if (file->f_pos > file_node->size) {
        file_node->size = file->f_pos;

        // update directory entry
        uint8_t sector[BLOCK_SIZE];
        readblock(file_node->dirent_cluster, sector);
        struct fat32_dirent* sector_dirent = (struct fat32_dirent*)sector;
        for (int i = 0; sector_dirent[i].name[0] != '\0'; i++) {
            // special value
            if (sector_dirent[i].name[0] == 0xE5) {
                continue;
            }
            // find target file directory entry
            if (((sector_dirent[i].cluster_high) << 16) | (sector_dirent[i].cluster_low) == file_node->first_cluster) {
                sector_dirent[i].size = (uint32_t)file->f_pos;
            }
        }
        writeblock(file_node->dirent_cluster, sector);
    }

    return file->f_pos - f_pos_ori;
}