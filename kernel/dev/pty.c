
#include <heap.h>
#include <stdlib.h>
#include <vfs.h>
#include <log.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <transfer.h>
#include <sys/stat.h>
#include <dirent.h>
#include <panic.h>
#include <thread.h>
#include <termios.h>
#include <blockingbuffer.h>

#define INTERNAL_BUFFER_SIZE 100
#define LINE_BUFFER_SIZE 400

/*
 * How PTYs work:
 * there is a subordinate and master end, both allow read and write
 * 
 * Subordinate end: like a 'terminal'. it reads what the master writes (and blocks for it), and writing to it
 *            writes to the screen. NEWLINE handling, backspace, etc. is handled here - it receives purely
 *            characters from the master. CANON and ECHO modes handled here
 * 
 * Master end: like a 'keyboard'. writing to it acts as sending keyboard presses to the screen. buffering is
 *             not done - i.e. every key (even e.g. BKSP, ^C) gets sent.
 *             reading from it? idk. probably nothing (or is it the screen??)
 */

struct pty_master_internal_data {
    struct vnode* subordinate;
    struct blocking_buffer* display_buffer;
    struct blocking_buffer* keybrd_buffer;
};

struct pty_subordinate_internal_data {
    struct vnode* master;
    struct termios termios;
    char line_buffer[LINE_BUFFER_SIZE];
    uint8_t line_buffer_char_width[LINE_BUFFER_SIZE];
    int line_buffer_pos;
};

// read copies data from trusted to untrusted.

// "THE SCREEN"
static int MasterRead(struct vnode* node, struct transfer* tr) {    
    struct pty_master_internal_data* internal = (struct pty_master_internal_data*) node->data;
    while (tr->length_remaining > 0) {
        char c = BlockingBufferGet(internal->display_buffer);
        PerformTransfer(&c, tr, 1);
    }

    return 0;
}

// "THE KEYBOARD"
static int MasterWrite(struct vnode* node, struct transfer* tr) {
    // TODO: (perhaps if it is full, it doesn't block, and just returns ENOBUFS

    struct pty_master_internal_data* internal = (struct pty_master_internal_data*) node->data;

    while (tr->length_remaining > 0) {
        char c;
        PerformTransfer(&c, tr, 1);
        BlockingBufferAdd(internal->keybrd_buffer, c);
    }

    return 0;
}

static void FlushSubordinateLineBuffer(struct vnode* node, struct transfer* tr) {
    struct pty_subordinate_internal_data* internal = (struct pty_subordinate_internal_data*) node->data;
    PerformTransfer(internal->line_buffer, tr, internal->line_buffer_pos);
    internal->line_buffer_pos = 0;
}

static void RemoveFromSubordinateLineBuffer(struct vnode* node) {
    struct pty_subordinate_internal_data* internal = (struct pty_subordinate_internal_data*) node->data;

    if (internal->line_buffer_pos == 0) {
        return;
    }

    internal->line_buffer[--internal->line_buffer_pos] = 0;
}

static void AddToSubordinateLineBuffer(struct vnode* node, char c, int width) {
    struct pty_subordinate_internal_data* internal = (struct pty_subordinate_internal_data*) node->data;

    if (internal->line_buffer_pos == LINE_BUFFER_SIZE) {
        Panic(PANIC_NOT_IMPLEMENTED);
        return;
    }

    internal->line_buffer[internal->line_buffer_pos] = c;
    internal->line_buffer_char_width[internal->line_buffer_pos] = width;
    internal->line_buffer_pos++;
}

// "THE STDIN LINE BUFFER"
static int SubordinateRead(struct vnode* node, struct transfer* tr) {    
    struct pty_subordinate_internal_data* internal = (struct pty_subordinate_internal_data*) node->data;
    struct pty_master_internal_data* master_internal = (struct pty_master_internal_data*) internal->master->data;

    bool echo = true;//internal->termios.c_lflag & ECHO;
    bool canon = true;//internal->termios.c_lflag & ICANON;

    // TODO: handle backspace (how does ICANON / cooked change this?)
    // TODO: handle TAB
    // TODO: handle control codes
    bool flushed_yet = false;
    while (true) {
        char c;
        if (flushed_yet) {
            int res = BlockingBufferTryGet(master_internal->keybrd_buffer, (uint8_t*) &c);
            if (res != 0) {
                return 0;
            }

        } else {
            c = BlockingBufferGet(master_internal->keybrd_buffer);
        }

        /*
         * This must happen before we modify the line buffer (i.e. to add or backspace a character), as
         * the backspace code here needs to check for a non-empty line (and so this must be done before we make 
         * the line empty).
         */
        if (echo) {
            if (c == '\b' && canon) {
                if (internal->line_buffer_pos > 0) {
                    BlockingBufferAdd(master_internal->display_buffer, '\b');
                    BlockingBufferAdd(master_internal->display_buffer, ' ');
                    BlockingBufferAdd(master_internal->display_buffer, '\b');
                }
            } else {
                BlockingBufferAdd(master_internal->display_buffer, c);
            }
        }

        if (c == '\b' && canon) {
            RemoveFromSubordinateLineBuffer(node);

        } else {
            AddToSubordinateLineBuffer(node, c, 1);
        }


        if (c == '\n' || c == 3 || !canon) {
            FlushSubordinateLineBuffer(node, tr);     
            flushed_yet = true;
        }
    }

    return 0;
}

// "WRITING TO STDOUT"
static int SubordinateWrite(struct vnode* node, struct transfer* tr) {
    struct pty_subordinate_internal_data* internal = (struct pty_subordinate_internal_data*) node->data;
    struct pty_master_internal_data* master_internal = (struct pty_master_internal_data*) internal->master->data;

    while (tr->length_remaining > 0) {
        char c;
        int err = PerformTransfer(&c, tr, 1);
        if (err) {
            return err;
        }

        // probably handle ANSI codes here too. how you let the master know about all that (e.g.
        // current colour), idk.
        BlockingBufferAdd(master_internal->display_buffer, c);
    }
    
    return 0;
}





static int MasterCheckOpen(struct vnode*, const char*, int) {
    return 0;
}

static int MasterIoctl(struct vnode*, int, void*) {
    return EINVAL;
}

static bool MasterIsSeekable(struct vnode*) {
    return false;
}

static int MasterIsTty(struct vnode*) {
    return false;
}

static int MasterCreate(struct vnode*, struct vnode**, const char*, int, mode_t) {
    return EINVAL;
}

static uint8_t MasterDirentType(struct vnode*) {
    return DT_CHR;
}

static int MasterStat(struct vnode*, struct stat* st) {
    st->st_mode = S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO;
    st->st_atime = 0;
    st->st_blksize = 0;
    st->st_blocks = 0;
    st->st_ctime = 0;
    st->st_dev = 0xBABECAFE;
    st->st_gid = 0;
    st->st_ino = 0xCAFEBABE;
    st->st_mtime = 0;
    st->st_nlink = 1;
    st->st_rdev = 0xCAFEDEAD;
    st->st_size = 0;
    st->st_uid = 0;
    return 0;
}

static int MasterTruncate(struct vnode*, off_t) {
    return EINVAL;
}

static int MasterClose(struct vnode*) {
    return 0;
}

static int MasterFollow(struct vnode*, struct vnode**, const char*) {
    return EINVAL;
}

static int MasterReaddir(struct vnode*, struct transfer*) {
    return EINVAL;
}

static int SubordinateCheckOpen(struct vnode*, const char*, int) {
    return 0;
}

static int SubordinateIoctl(struct vnode*, int, void*) {
    return EINVAL;
}

static bool SubordinateIsSeekable(struct vnode*) {
    return false;
}

static int SubordinateIsTty(struct vnode*) {
    return true;
}

static int SubordinateCreate(struct vnode*, struct vnode**, const char*, int, mode_t) {
    return EINVAL;
}

static uint8_t SubordinateDirentType(struct vnode*) {
    return DT_CHR;
}

static int SubordinateStat(struct vnode*, struct stat* st) {
    st->st_mode = S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO;
    st->st_atime = 0;
    st->st_blksize = 0;
    st->st_blocks = 0;
    st->st_ctime = 0;
    st->st_dev = 0xBABECAFE;
    st->st_gid = 0;
    st->st_ino = 0xCAFEBABE;
    st->st_mtime = 0;
    st->st_nlink = 1;
    st->st_rdev = 0xCAFEDEAD;
    st->st_size = 0;
    st->st_uid = 0;
    return 0;
}

static int SubordinateTruncate(struct vnode*, off_t) {
    return EINVAL;
}

static int SubordinateClose(struct vnode*) {
    return 0;
}

static int SubordinateFollow(struct vnode*, struct vnode**, const char*) {
    return EINVAL;
}

static int SubordinateReaddir(struct vnode*, struct transfer*) {
    return EINVAL;
}

static const struct vnode_operations master_operations = {
    .check_open     = MasterCheckOpen,
    .ioctl          = MasterIoctl,
    .is_seekable    = MasterIsSeekable,
    .is_tty         = MasterIsTty,
    .read           = MasterRead,
    .write          = MasterWrite,
    .close          = MasterClose,
    .truncate       = MasterTruncate,
    .create         = MasterCreate,
    .follow         = MasterFollow,
    .dirent_type    = MasterDirentType,
    .readdir        = MasterReaddir,
    .stat           = MasterStat,
};

static const struct vnode_operations subordinate_operations = {
    .check_open     = SubordinateCheckOpen,
    .ioctl          = SubordinateIoctl,
    .is_seekable    = SubordinateIsSeekable,
    .is_tty         = SubordinateIsTty,
    .read           = SubordinateRead,
    .write          = SubordinateWrite,
    .close          = SubordinateClose,
    .truncate       = SubordinateTruncate,
    .create         = SubordinateCreate,
    .follow         = SubordinateFollow,
    .dirent_type    = SubordinateDirentType,
    .readdir        = SubordinateReaddir,
    .stat           = SubordinateStat,
};

void CreatePseudoTerminal(struct vnode** master, struct vnode** subordinate) {
    struct vnode* m = CreateVnode(master_operations);
    struct vnode* s = CreateVnode(subordinate_operations);
    
    struct pty_master_internal_data* m_data = AllocHeap(sizeof(struct pty_master_internal_data));
    struct pty_subordinate_internal_data* s_data = AllocHeap(sizeof(struct pty_subordinate_internal_data));

    m_data->subordinate = s;
    m_data->display_buffer = BlockingBufferCreate(INTERNAL_BUFFER_SIZE);
    m_data->keybrd_buffer = BlockingBufferCreate(INTERNAL_BUFFER_SIZE);

    s_data->master = m;
    s_data->termios.c_lflag = ICANON | ECHO;

    m->data = m_data;
    s->data = s_data;
    *master = m;
    *subordinate = s;
}