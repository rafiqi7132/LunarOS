/**
 * vfs.c - LunarOS Virtual File System
 *
 * VFS adalah lapisan abstraksi di atas filesystem nyata.
 * Semua syscall file (open, read, write, dll) masuk ke sini,
 * lalu VFS mendelegasikan ke filesystem yang tepat
 * (LunarFS, tmpfs, dll) via function pointer di vfs_ops_t.
 *
 * Mirip cara kerja VFS di Linux / XNU (iOS).
 *
 * Lokasi: src/fs/vfs.c
 */

#include "vfs.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"
#include "../drivers/arm_timer.h"
#include "../mm/heap.h"

/* ─────────────────────────────────────────────
   State VFS
   ───────────────────────────────────────────── */
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_fd_t    fd_table[VFS_MAX_FD];
static char        cwd[VFS_MAX_PATH];     /* current working directory */
static uint64_t    next_inode = 1;
static int         vfs_ready  = 0;

/* Root node dari mount "/" */
static vfs_node_t *vfs_root = NULL;

/* ─────────────────────────────────────────────
   Forward declaration LunarFS
   ───────────────────────────────────────────── */
extern int       lunfs_mount(vfs_node_t **root_out);
extern vfs_ops_t lunfs_ops;

/* ─────────────────────────────────────────────
   Helper: string utils (tanpa libc)
   ───────────────────────────────────────────── */
static int vfs_strlen(const char *s) {
    int i = 0; while (s[i]) i++; return i;
}

static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void vfs_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int vfs_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i])   return a[i] - b[i];
    }
    return 0;
}

/* ─────────────────────────────────────────────
   Helper: alokasi inode baru
   ───────────────────────────────────────────── */
static uint64_t alloc_inode(void) {
    return next_inode++;
}

/* ─────────────────────────────────────────────
   Helper: alokasi dan init vfs_node baru
   ───────────────────────────────────────────── */
vfs_node_t *vfs_node_alloc(const char *name, uint32_t type,
                             uint32_t perm, vfs_ops_t *ops)
{
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    vfs_strncpy(node->name, name, VFS_MAX_NAME);
    node->type        = type;
    node->permissions = perm;
    node->uid         = 0;
    node->inode       = alloc_inode();
    node->created_at  = arm_timer_get_uptime_ms();
    node->modified_at = node->created_at;
    node->ops         = ops;
    node->fs_data     = NULL;
    node->parent      = NULL;
    node->children    = NULL;
    node->next_sibling= NULL;
    node->size        = 0;

    return node;
}

/* ─────────────────────────────────────────────
   Helper: tambah child ke direktori
   ───────────────────────────────────────────── */
void vfs_node_add_child(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child) return;
    child->parent       = parent;
    child->next_sibling = parent->children;
    parent->children    = child;
}

/* ─────────────────────────────────────────────
   vfs_init()
   ───────────────────────────────────────────── */
int vfs_init(void) {
    /* Reset tabel mount */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = 0;
        mounts[i].root   = NULL;
    }

    /* Reset tabel fd */
    for (int i = 0; i < VFS_MAX_FD; i++) {
        fd_table[i].active = 0;
        fd_table[i].node   = NULL;
        fd_table[i].offset = 0;
    }

    /* CWD awal = root */
    vfs_strncpy(cwd, "/", VFS_MAX_PATH);

    vfs_ready = 1;
    pl011_puts("[VFS] Initialized\n");
    return 0;
}

/* ─────────────────────────────────────────────
   vfs_mount()
   Mount filesystem ke path tertentu
   ───────────────────────────────────────────── */
int vfs_mount(const char *path, uint32_t fs_type, uint32_t flags) {
    (void)flags;

    /* Cari slot mount kosong */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        pl011_puts("[VFS] ERROR: mount table penuh\n");
        return -1;
    }

    vfs_node_t *root = NULL;
    int r = -1;

    switch (fs_type) {
    case FS_TYPE_LUNFS:
        r = lunfs_mount(&root);
        break;
    default:
        pl011_puts("[VFS] ERROR: fs_type tidak dikenal\n");
        return -1;
    }

    if (r != 0 || !root) {
        pl011_puts("[VFS] ERROR: mount gagal\n");
        return -1;
    }

    vfs_strncpy(mounts[slot].path, path, VFS_MAX_PATH);
    mounts[slot].fs_type = fs_type;
    mounts[slot].root    = root;
    mounts[slot].active  = 1;

    /* Jika mount di "/" jadikan vfs_root */
    if (vfs_strcmp(path, "/") == 0) {
        vfs_root = root;
    }

    pl011_printf("[VFS] Mounted fs_type=%u at %s\n", fs_type, path);
    return 0;
}

/* ─────────────────────────────────────────────
   vfs_umount()
   ───────────────────────────────────────────── */
int vfs_umount(const char *path) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active &&
            vfs_strcmp(mounts[i].path, path) == 0)
        {
            /* Sync dulu sebelum unmount */
            if (mounts[i].root && mounts[i].root->ops &&
                mounts[i].root->ops->sync) {
                mounts[i].root->ops->sync(mounts[i].root);
            }
            mounts[i].active = 0;
            mounts[i].root   = NULL;
            pl011_printf("[VFS] Unmounted %s\n", path);
            return 0;
        }
    }
    return -1;
}

/* ─────────────────────────────────────────────
   vfs_resolve_path()
   Ubah path string jadi vfs_node_t*
   ───────────────────────────────────────────── */
vfs_node_t *vfs_resolve_path(const char *path) {
    if (!vfs_root || !path) return NULL;

    /* Path relatif — prepend CWD */
    char full[VFS_MAX_PATH];
    if (path[0] != '/') {
        /* Gabung CWD + path */
        int cwdlen = vfs_strlen(cwd);
        int plen   = vfs_strlen(path);
        if (cwdlen + plen + 2 >= VFS_MAX_PATH) return NULL;

        int i = 0;
        while (cwd[i]) { full[i] = cwd[i]; i++; }
        if (cwd[i-1] != '/') full[i++] = '/';
        int j = 0;
        while (path[j]) { full[i++] = path[j++]; }
        full[i] = '\0';
        path = full;
    }

    /* Skip leading '/' */
    if (path[0] == '/' && path[1] == '\0') return vfs_root;

    const char *p   = path + 1;   /* skip pertama '/' */
    vfs_node_t *cur = vfs_root;

    while (*p && cur) {
        /* Ambil satu komponen path */
        char  component[VFS_MAX_NAME];
        int   ci = 0;

        while (*p && *p != '/') {
            if (ci < VFS_MAX_NAME - 1) component[ci++] = *p;
            p++;
        }
        component[ci] = '\0';
        if (*p == '/') p++;

        if (ci == 0) continue;

        /* Handle "." dan ".." */
        if (vfs_strcmp(component, ".") == 0) continue;
        if (vfs_strcmp(component, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }

        /* Cari di children */
        if (!cur->ops || !cur->ops->finddir) return NULL;
        cur = cur->ops->finddir(cur, component);
    }

    return cur;
}

/* ─────────────────────────────────────────────
   Helper: cari parent dir dan nama file
   dari path "/a/b/c" → parent="/a/b", name="c"
   ───────────────────────────────────────────── */
static vfs_node_t *resolve_parent(const char *path, char *name_out) {
    /* Cari '/' terakhir */
    int  len      = vfs_strlen(path);
    int  last_sep = 0;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') last_sep = i;
    }

    /* Salin nama file */
    vfs_strncpy(name_out, path + last_sep + 1, VFS_MAX_NAME);

    /* Resolve parent path */
    if (last_sep == 0) return vfs_root;   /* parent adalah root */

    char parent_path[VFS_MAX_PATH];
    vfs_strncpy(parent_path, path, last_sep + 1);
    parent_path[last_sep] = '\0';
    return vfs_resolve_path(parent_path);
}

/* ─────────────────────────────────────────────
   Helper: alokasi file descriptor
   ───────────────────────────────────────────── */
static int alloc_fd(vfs_node_t *node, uint32_t flags) {
    /* fd 0,1,2 = stdin,stdout,stderr — skip */
    for (int i = 3; i < VFS_MAX_FD; i++) {
        if (!fd_table[i].active) {
            fd_table[i].node   = node;
            fd_table[i].flags  = flags;
            fd_table[i].offset = 0;
            fd_table[i].active = 1;
            return i;
        }
    }
    return -1;
}

/* ─────────────────────────────────────────────
   vfs_open()
   ───────────────────────────────────────────── */
int vfs_open(const char *path, uint32_t flags) {
    if (!vfs_ready || !path) return -1;

    vfs_node_t *node = vfs_resolve_path(path);

    /* File tidak ada tapi ada O_CREAT */
    if (!node && (flags & O_CREAT)) {
        char        name[VFS_MAX_NAME];
        vfs_node_t *parent = resolve_parent(path, name);
        if (!parent) return -1;
        if (!parent->ops || !parent->ops->mkdir) return -1;

        /* Buat file baru via mkdir-like call */
        if (parent->ops->open) {
            /* LunarFS akan buat file baru */
            node = vfs_node_alloc(name, VFS_NODE_FILE,
                                  PERM_OWNER_RW | PERM_GROUP_R,
                                  parent->ops);
            if (!node) return -1;
            vfs_node_add_child(parent, node);
        }
    }

    if (!node) return -1;

    /* O_TRUNC — truncate isi file */
    if ((flags & O_TRUNC) && node->type == VFS_NODE_FILE) {
        node->size = 0;
    }

    /* Panggil open() filesystem */
    if (node->ops && node->ops->open) {
        int r = node->ops->open(node, flags);
        if (r != 0) return -1;
    }

    int fd = alloc_fd(node, flags);
    if (fd < 0) {
        pl011_puts("[VFS] ERROR: fd table penuh\n");
        return -1;
    }

    /* O_APPEND — offset ke akhir */
    if (flags & O_APPEND) {
        fd_table[fd].offset = node->size;
    }

    return fd;
}

/* ─────────────────────────────────────────────
   vfs_close()
   ───────────────────────────────────────────── */
int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD) return -1;
    if (!fd_table[fd].active) return -1;

    vfs_node_t *node = fd_table[fd].node;
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }

    fd_table[fd].active = 0;
    fd_table[fd].node   = NULL;
    fd_table[fd].offset = 0;
    return 0;
}

/* ─────────────────────────────────────────────
   vfs_read()
   ───────────────────────────────────────────── */
int64_t vfs_read(int fd, void *buf, uint64_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD) return -1;
    if (!fd_table[fd].active)       return -1;
    if (!buf || size == 0)          return 0;

    vfs_fd_t   *f    = &fd_table[fd];
    vfs_node_t *node = f->node;

    /* Cek permission */
    if (!(f->flags & O_RDONLY) && !(f->flags & O_RDWR)) return -1;
    if (!node->ops || !node->ops->read) return -1;

    int64_t n = node->ops->read(node, buf, size, f->offset);
    if (n > 0) f->offset += n;

    return n;
}

/* ─────────────────────────────────────────────
   vfs_write()
   ───────────────────────────────────────────── */
int64_t vfs_write(int fd, const void *buf, uint64_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD) return -1;
    if (!fd_table[fd].active)       return -1;
    if (!buf || size == 0)          return 0;

    vfs_fd_t   *f    = &fd_table[fd];
    vfs_node_t *node = f->node;

    /* Cek permission */
    if (!(f->flags & O_WRONLY) && !(f->flags & O_RDWR)) return -1;
    if (!node->ops || !node->ops->write) return -1;

    int64_t n = node->ops->write(node, buf, size, f->offset);
    if (n > 0) {
        f->offset += n;
        if (f->offset > node->size) {
            node->size        = (uint32_t)f->offset;
            node->modified_at = arm_timer_get_uptime_ms();
        }
    }

    return n;
}

/* ─────────────────────────────────────────────
   vfs_seek()
   ───────────────────────────────────────────── */
int64_t vfs_seek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FD) return -1;
    if (!fd_table[fd].active)       return -1;

    vfs_fd_t *f = &fd_table[fd];
    int64_t   new_offset;

    switch (whence) {
    case SEEK_SET: new_offset = offset;                         break;
    case SEEK_CUR: new_offset = (int64_t)f->offset + offset;   break;
    case SEEK_END: new_offset = (int64_t)f->node->size + offset; break;
    default: return -1;
    }

    if (new_offset < 0) return -1;
    f->offset = (uint64_t)new_offset;
    return new_offset;
}

/* ─────────────────────────────────────────────
   vfs_stat()
   ───────────────────────────────────────────── */
int vfs_stat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return -1;

    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -1;

    if (node->ops && node->ops->stat) {
        return node->ops->stat(node, out);
    }

    /* Fallback: isi dari node langsung */
    out->type        = node->type;
    out->permissions = node->permissions;
    out->uid         = node->uid;
    out->size        = node->size;
    out->created_at  = node->created_at;
    out->modified_at = node->modified_at;
    return 0;
}

/* ─────────────────────────────────────────────
   vfs_unlink() — hapus file
   ───────────────────────────────────────────── */
int vfs_unlink(const char *path) {
    if (!path) return -1;

    char        name[VFS_MAX_NAME];
    vfs_node_t *parent = resolve_parent(path, name);
    if (!parent) return -1;

    if (!parent->ops || !parent->ops->unlink) return -1;
    return parent->ops->unlink(parent, name);
}

/* ─────────────────────────────────────────────
   vfs_mkdir()
   ───────────────────────────────────────────── */
int vfs_mkdir(const char *path, uint32_t perm) {
    if (!path) return -1;

    char        name[VFS_MAX_NAME];
    vfs_node_t *parent = resolve_parent(path, name);
    if (!parent) return -1;
    if (parent->type != VFS_NODE_DIR) return -1;

    if (!parent->ops || !parent->ops->mkdir) return -1;
    return parent->ops->mkdir(parent, name, perm);
}

/* ─────────────────────────────────────────────
   vfs_readdir()
   ───────────────────────────────────────────── */
int vfs_readdir(const char *path, uint32_t index, char *name_out) {
    if (!path || !name_out) return -1;

    vfs_node_t *node = vfs_resolve_path(path);
    if (!node || node->type != VFS_NODE_DIR) return -1;

    if (!node->ops || !node->ops->readdir) return -1;
    return node->ops->readdir(node, index, name_out);
}

/* ─────────────────────────────────────────────
   vfs_chdir() / vfs_getcwd()
   ───────────────────────────────────────────── */
int vfs_chdir(const char *path) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node || node->type != VFS_NODE_DIR) return -1;

    vfs_strncpy(cwd, path, VFS_MAX_PATH);
    return 0;
}

int vfs_getcwd(char *buf, uint64_t size) {
    if (!buf) return -1;
    int cwdlen = vfs_strlen(cwd);
    int copylen = (cwdlen < (int)size - 1) ? cwdlen : (int)size - 1;
    for (int i = 0; i < copylen; i++) buf[i] = cwd[i];
    buf[copylen] = '\0';
    return 0;
}

/* ─────────────────────────────────────────────
   vfs_sync() — flush semua filesystem
   ───────────────────────────────────────────── */
void vfs_sync(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active || !mounts[i].root) continue;
        vfs_node_t *root = mounts[i].root;
        if (root->ops && root->ops->sync) {
            root->ops->sync(root);
        }
    }
    pl011_puts("[VFS] sync done\n");
}