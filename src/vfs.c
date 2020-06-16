#include "vfs.h"

#include "mm.h"
#include "my_string.h"
#include "tmpfs.h"
#include "uart0.h"
#include "util.h"

struct mount* rootfs;

void rootfs_init() {
    struct filesystem* tmpfs = (struct filesystem*)kmalloc(sizeof(struct filesystem));
    tmpfs->name = (char*)kmalloc(sizeof(char) * 6);
    strcpy(tmpfs->name, "tmpfs");
    tmpfs->setup_mount = tmpfs_setup_mount;
    register_filesystem(tmpfs);

    rootfs = (struct mount*)kmalloc(sizeof(struct mount));
    tmpfs->setup_mount(tmpfs, rootfs);
}

int register_filesystem(struct filesystem* fs) {
    // register the file system to the kernel.
    // you can also initialize memory pool of the file system here.
    if (!strcmp(fs->name, "tmpfs")) {
        uart_printf("\n[%f] Register tmpfs", get_timestamp());
        return tmpfs_register();
    }
    return -1;
}

struct file* create_fd(struct vnode* target) {
    struct file* fd = (struct file*)kmalloc(sizeof(struct file));
    fd->f_ops = target->f_ops;
    fd->vnode = target;
    fd->f_pos = 0;
    return fd;
}

struct file* vfs_open(const char* pathname, int flags) {
    // 1. Lookup pathname from the root vnode.
    struct vnode* dir = rootfs->root->vnode;
    struct vnode* target;
    // 2. Create a new file descriptor for this vnode if found.
    if (rootfs->root->vnode->v_ops->lookup(dir, &target, pathname) == 0) {
        return create_fd(target);
    }
    // 3. Create a new file if O_CREAT is specified in flags.
    else {
        if (flags & O_CREAT) {
            rootfs->root->vnode->v_ops->create(dir, &target, pathname);
            return create_fd(target);
        }
        else {
            return NULL;
        }
    }
}

int vfs_close(struct file* file) {
    // 1. release the file descriptor
}

int vfs_write(struct file* file, const void* buf, uint64_t len) {
    // 1. write len byte from buf to the opened file.
    // 2. return written size or error code if an error occurs.
}

int vfs_read(struct file* file, void* buf, uint64_t len) {
    // 1. read min(len, readable file data size) byte to buf from the opened file.
    // 2. return read size or error code if an error occurs.
}