/**
 * lunfs.h - LunarFS Filesystem Header
 * Filesystem bawaan LunarOS — disimpan di RAM
 * Lokasi: src/fs/lunfs.h
 */

#ifndef LUNFS_H
#define LUNFS_H

#include <stdint.h>
#include "vfs.h"

/* ─────────────────────────────────────────────
   Konstanta LunarFS
   ───────────────────────────────────────────── */
#define LUNFS_MAX_NODES     256      /* max file + dir */
#define LUNFS_MAX_FILE_SIZE (4 * 1024 * 1024)   /* 4 MB per file */
#define LUNFS_BLOCK_SIZE    512
#define LUNFS_MAGIC         0x4C554E41   /* "LUNA" */
#define LUNFS_VERSION       1

/* ─────────────────────────────────────────────
   LunarFS node internal
   ───────────────────────────────────────────── */
typedef struct lunfs_node {
    int       used;                  /* slot ini terpakai? */
    uint32_t  type;                  /* VFS_NODE_FILE / DIR */
    char      name[VFS_MAX_NAME];
    uint32_t  permissions;
    uint32_t  uid;
    uint32_t  size;
    uint8_t  *data;                  /* isi file (alokasi dari heap) */
    uint32_t  data_capacity;         /* berapa byte yang dialokasi */
    uint64_t  inode;
    uint64_t  created_at;
    uint64_t  modified_at;

    struct lunfs_node *parent;
    struct lunfs_node *children;
    struct lunfs_node *next_sibling;

    vfs_node_t *vnode;              /* VFS node yang terkait */
} lunfs_node_t;

/* ─────────────────────────────────────────────
   Superblock LunarFS
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t total_nodes;
    uint32_t used_nodes;
    uint64_t created_at;
} lunfs_superblock_t;

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */
int  lunfs_mount(vfs_node_t **root_out);
void lunfs_dump_tree(void);          /* debug: tampilkan seluruh tree */

/* vfs_ops_t yang diisi LunarFS */
extern vfs_ops_t lunfs_ops;

#endif /* LUNFS_H */