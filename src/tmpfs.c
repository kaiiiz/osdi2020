#include "tmpfs.h"

#include "list.h"
#include "mm.h"
#include "my_string.h"
#include "uart0.h"

struct vnode_operations* tmpfs_v_ops;
struct file_operations* tmpfs_f_ops;

struct vnode* tmpfs_create_vnode(struct dentry* dentry) {
    struct vnode* vnode = (struct vnode*)kmalloc(sizeof(struct vnode));
    vnode->dentry = dentry;
    vnode->f_ops = tmpfs_f_ops;
    vnode->v_ops = tmpfs_v_ops;
    return vnode;
}

struct dentry* tmpfs_create_dentry(struct dentry* parent, const char* name) {
    struct dentry* dentry = (struct dentry*)kmalloc(sizeof(struct dentry));
    strcpy(dentry->name, name);
    dentry->parent = parent;
    list_head_init(&dentry->list);
    if (parent == NULL) {
        list_head_init(&dentry->childs);
    }
    else {
        list_add(&dentry->list, &parent->childs);
    }
    dentry->vnode = tmpfs_create_vnode(dentry);
    return dentry;
}

int tmpfs_register() {
    tmpfs_v_ops = (struct vnode_operations*)kmalloc(sizeof(struct vnode_operations));
    tmpfs_v_ops->create = tmpfs_create;
    tmpfs_v_ops->lookup = tmpfs_lookup;
    tmpfs_f_ops = (struct file_operations*)kmalloc(sizeof(struct file_operations));
    tmpfs_f_ops->read = tmpfs_read;
    tmpfs_f_ops->write = tmpfs_write;
    return 0;
}

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mount) {
    mount->fs = fs;
    mount->root = tmpfs_create_dentry(NULL, "/");
}

// vnode operations
int tmpfs_lookup(struct vnode* dir, struct vnode** target, const char* component_name) {
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

int tmpfs_create(struct vnode* dir, struct vnode** target, const char* component_name) {
    // create tmpfs node structure
    struct tmpfs_node* tmpfs_node = (struct tmpfs_node*)kmalloc(sizeof(struct tmpfs_node));
    tmpfs_node->flag = REGULAR_FILE;

    // create child dentry
    struct dentry* d_child = tmpfs_create_dentry(dir->dentry, component_name);
    d_child->vnode->internal = (void*)tmpfs_node;

    *target = d_child->vnode;
    return 0;
}

// file operations
int tmpfs_read(struct file* file, void* buf, uint64_t len) {
}

int tmpfs_write(struct file* file, const void* buf, uint64_t len) {
}