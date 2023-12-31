
#include <heap.h>
#include <log.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <vfs.h>
#include <transfer.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <fs/demofs/demofs_private.h>

struct vnode_data {
    ino_t inode;
    struct demofs fs;
    uint32_t file_length;
    bool directory;
};

static int CheckOpen(struct vnode*, const char* name, int flags) {
    if (strlen(name) >= MAX_NAME_LENGTH) {
        return ENAMETOOLONG;
    }

    if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
        return EROFS;
    }

    return 0;
}

static int Ioctl(struct vnode*, int, void*) {
    return EINVAL;
}

static bool IsSeekable(struct vnode*) {
    return true;
}

static int IsTty(struct vnode*) {
    return false;
}

static int Read(struct vnode* node, struct transfer* io) {    
    struct vnode_data* data = node->data;
    if (data->directory) {
        return EISDIR;
    }

    return demofs_read_file(&data->fs, data->inode, data->file_length, io);
}

static int Readdir(struct vnode* node, struct transfer* io) {
    struct vnode_data* data = node->data;
    if (!data->directory) {
        return ENOTDIR;
    }

    return demofs_read_directory_entry(&data->fs, data->inode, io);
}

static int Write(struct vnode*, struct transfer*) {
    return EROFS;
}

static int Create(struct vnode*, struct vnode**, const char*, int, mode_t) {
    return EROFS;
}

static uint8_t DirentType(struct vnode* node) {
    struct vnode_data* data = node->data;
    return data->directory ? DT_DIR : DT_REG;
}

static int Stat(struct vnode* node, struct stat* stat) {
    struct vnode_data* data = node->data;

    stat->st_atime = 0;
    stat->st_blksize = 512;
    stat->st_blocks = 0;
    stat->st_ctime = 0;
    stat->st_dev = 0xDEADDEAD;
    stat->st_gid = 0;
    stat->st_ino = data->inode;
    stat->st_mode = (INODE_IS_DIR(data->inode) ? S_IFDIR : S_IFREG) | S_IRWXU | S_IRWXG | S_IRWXO;
    stat->st_mtime = 0;
    stat->st_nlink = 1;
    stat->st_rdev = 0;
    stat->st_size = data->file_length;
    stat->st_uid = 0;

    return 0;
}

static int Truncate(struct vnode*, off_t) {
    return EROFS;
}

static int Close(struct vnode* node) {
    FreeHeap(node->data);
    return 0;
}

static struct vnode* CreateDemoFsVnode();

static int Follow(struct vnode* node, struct vnode** out, const char* name) {
    struct vnode_data* data = node->data;
    if (data->directory) {
        ino_t child_inode;
        uint32_t file_length;

        int status = demofs_follow(&data->fs, data->inode, &child_inode, name, &file_length);
        if (status != 0) {
            return status;
        }
        
        /*
        * TODO: return existing vnode if someone opens the same file twice...
        */
    
        struct vnode* child_node = CreateDemoFsVnode();
        struct vnode_data* child_data = AllocHeap(sizeof(struct vnode_data));
        child_data->inode = child_inode;
        child_data->fs = data->fs;
        child_data->file_length = file_length;
        child_data->directory = INODE_IS_DIR(child_inode);

        child_node->data = child_data;

        *out = child_node;

        return 0;

    } else {
        return ENOTDIR;
    }
}

static const struct vnode_operations dev_ops = {
    .check_open     = CheckOpen,
    .ioctl          = Ioctl,
    .is_seekable    = IsSeekable,
    .is_tty         = IsTty,
    .read           = Read,
    .write          = Write,
    .close          = Close,
    .truncate       = Truncate,
    .create         = Create,
    .follow         = Follow,
    .dirent_type    = DirentType,
    .readdir        = Readdir,
    .stat           = Stat,
};

static struct vnode* CreateDemoFsVnode() {
    return CreateVnode(dev_ops);
}

int DemofsMountCreator(struct open_file* raw_device, struct open_file** out) {   
    // TODO: you'd actually want to check if the disk is a DemoFs disk, and return something non-zero if so
    
	struct vnode* node = CreateDemoFsVnode();
    struct vnode_data* data = AllocHeap(sizeof(struct vnode_data));
    
    data->fs.disk = raw_device;
    data->fs.root_inode = 9 | (1 << 31);
    data->inode = 9 | (1 << 31);        /* root directory inode */
    data->file_length = 0;              /* root directory has no length */
    data->directory = true;

    node->data = data;

	*out = CreateOpenFile(node, 0, 0, true, false);
    return 0;
}