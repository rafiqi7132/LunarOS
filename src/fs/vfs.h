/**
 * vfs.h - LunarOS Virtual File System Header
 * Lokasi: src/fs/vfs.h
 */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Konstanta
   ───────────────────────────────────────────── */
#define VFS_MAX_FD          64       /* max file descriptor per sistem */
#define VFS_MAX_MOUNTS      8        /* max filesystem yang di-mount */
#define VFS_MAX_PATH        256
#define VFS_MAX_NAME        64

/* Tipe filesystem */
#define FS_TYPE_LUNFS       1        /* LunarFS — filesystem sendiri */
#define FS_TYPE_TMPFS       2        /* tmpfs — RAM filesystem */

/* Flag open() */
#define O_RDONLY            0x0001
#define O_WRONLY            0x0002
#define O_RDWR              0x0003
#define O_CREAT             0x0100
#define O_TRUNC             0x0200
#define O_APPEND            0x0400

/* Seek mode */
#define SEEK_SET            0
#define SEEK_CUR            1
#define SEEK_END            2

/* Permission bits */
#define PERM_OWNER_RWX      0700
#define PERM_OWNER_RW       0600
#define PERM_OWNER_RX       0500
#define PERM_OWNER_R        0400
#define PERM_GROUP_RWX      0070
#define PERM_GROUP_RX       0050
#define PERM_GROUP_R        0040
#define PERM_OTHER_RWX      0007
#define PERM_OTHER_RX       0005
#define PERM_OTHER_R        0004
#define PERM_OTHER_NONE     0000

/* Tipe node */
#define VFS_NODE_FILE       1
#define VFS_NODE_DIR        2
#define VFS_NODE_SYMLINK    3
#define VFS_NODE_DEVICE     4

/* ACL flag — dipakai syscall_check */
#define ACL_READ            0x4
#define ACL_WRITE           0x2
#define ACL_EXEC            0x1

/* ─────────────────────────────────────────────
   Struct stat — info file
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t  type;         /* VFS_NODE_FILE / DIR / dll */
    uint32_t  permissions;
    uint32_t  uid;
    uint32_t  size;
    uint64_t  created_at;
    uint64_t  modified_at;
} vfs_stat_t;

/* ─────────────────────────────────────────────
   Forward declaration
   ───────────────────────────────────────────── */
struct vfs_node;
struct vfs_fs;

/* ─────────────────────────────────────────────
   Operasi filesystem — function pointer table
   Setiap filesystem (LunarFS, tmpfs, dll)
   mengisi struct ini dengan fungsinya sendiri
   ───────────────────────────────────────────── */
typedef struct vfs_ops {
    int      (*open)   (struct vfs_node *node, uint32_t flags);
    int      (*close)  (struct vfs_node *node);
    int64_t  (*read)   (struct vfs_node *node, void *buf,
                        uint64_t size, uint64_t offset);
    int64_t  (*write)  (struct vfs_node *node, const void *buf,
                        uint64_t size, uint64_t offset);
    int      (*mkdir)  (struct vfs_node *parent, const char *name,
                        uint32_t perm);
    int      (*unlink) (struct vfs_node *parent, const char *name);
    struct vfs_node *(*finddir)(struct vfs_node *dir, const char *name);
    int      (*readdir)(struct vfs_node *dir, uint32_t index,
                        char *name_out);
    int      (*stat)   (struct vfs_node *node, vfs_stat_t *out);
    int      (*sync)   (struct vfs_node *node);
} vfs_ops_t;

/* ─────────────────────────────────────────────
   VFS Node — satu file atau direktori
   ───────────────────────────────────────────── */
typedef struct vfs_node {
    char            name[VFS_MAX_NAME];
    uint32_t        type;           /* VFS_NODE_FILE / DIR / dll */
    uint32_t        permissions;
    uint32_t        uid;
    uint32_t        size;
    uint64_t        inode;          /* nomor inode unik */
    uint64_t        created_at;
    uint64_t        modified_at;

    vfs_ops_t       *ops;           /* operasi filesystem */
    void            *fs_data;       /* data internal filesystem */

    struct vfs_node *parent;
    struct vfs_node *children;      /* linked list anak (untuk dir) */
    struct vfs_node *next_sibling;  /* sibling berikutnya */
} vfs_node_t;

/* ─────────────────────────────────────────────
   Mount point
   ───────────────────────────────────────────── */
typedef struct {
    char        path[VFS_MAX_PATH];
    uint32_t    fs_type;
    vfs_node_t *root;
    int         active;
} vfs_mount_t;

/* ─────────────────────────────────────────────
   File Descriptor — satu entri per open()
   ───────────────────────────────────────────── */
typedef struct {
    vfs_node_t *node;
    uint32_t    flags;
    uint64_t    offset;     /* posisi baca/tulis saat ini */
    int         active;
} vfs_fd_t;

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */

/* Init & mount */
int  vfs_init(void);
int  vfs_mount(const char *path, uint32_t fs_type, uint32_t flags);
int  vfs_umount(const char *path);
void vfs_sync(void);

/* File operations */
int     vfs_open   (const char *path, uint32_t flags);
int     vfs_close  (int fd);
int64_t vfs_read   (int fd, void *buf, uint64_t size);
int64_t vfs_write  (int fd, const void *buf, uint64_t size);
int64_t vfs_seek   (int fd, int64_t offset, int whence);
int     vfs_stat   (const char *path, vfs_stat_t *out);
int     vfs_unlink (const char *path);

/* Directory operations */
int  vfs_mkdir  (const char *path, uint32_t perm);
int  vfs_readdir(const char *path, uint32_t index, char *name_out);
int  vfs_chdir  (const char *path);
int  vfs_getcwd (char *buf, uint64_t size);

/* Node lookup */
vfs_node_t *vfs_resolve_path(const char *path);

#endif /* VFS_H */