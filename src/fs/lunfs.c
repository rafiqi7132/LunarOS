/**
 * lunfs.c - LunarFS Filesystem Implementation
 *
 * LunarFS adalah filesystem in-memory milik LunarOS.
 * Semua data disimpan di RAM (heap kernel) — tidak ada
 * persistensi ke disk untuk versi ini.
 *
 * Fitur:
 *   - Direktori bertingkat (tree)
 *   - File read/write dengan auto-resize
 *   - Permission bits (owner/group/other)
 *   - Timestamp created/modified
 *   - Terintegrasi penuh dengan VFS layer
 *
 * Lokasi: src/fs/lunfs.c
 */

#include "lunfs.h"
#include "vfs.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"
#include "../drivers/arm_timer.h"
#include "../mm/heap.h"

/* ─────────────────────────────────────────────
   State LunarFS
   ───────────────────────────────────────────── */
static lunfs_node_t  node_pool[LUNFS_MAX_NODES];
static lunfs_superblock_t superblock;
static int           lunfs_mounted = 0;
static uint64_t      next_inode    = 1;

/* ─────────────────────────────────────────────
   String helpers
   ───────────────────────────────────────────── */
static int lfs_strlen(const char *s) {
    int i = 0; while (s[i]) i++; return i;
}
static int lfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}
static void lfs_strncpy(char *d, const char *s, int n) {
    int i = 0;
    while (i < n-1 && s[i]) { d[i]=s[i]; i++; }
    d[i] = '\0';
}
static void lfs_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}
static void lfs_memset(void *dst, uint8_t val, uint64_t n) {
    uint8_t *d = dst;
    for (uint64_t i = 0; i < n; i++) d[i] = val;
}

/* ─────────────────────────────────────────────
   Pool management
   ───────────────────────────────────────────── */
static lunfs_node_t *lfs_alloc_node(void) {
    for (int i = 0; i < LUNFS_MAX_NODES; i++) {
        if (!node_pool[i].used) {
            lfs_memset(&node_pool[i], 0, sizeof(lunfs_node_t));
            node_pool[i].used   = 1;
            node_pool[i].inode  = next_inode++;
            superblock.used_nodes++;
            return &node_pool[i];
        }
    }
    pl011_puts("[LUNFS] ERROR: node pool penuh\n");
    return NULL;
}

static void lfs_free_node(lunfs_node_t *node) {
    if (!node) return;
    if (node->data) {
        kfree(node->data);
        node->data          = NULL;
        node->data_capacity = 0;
    }
    node->used = 0;
    superblock.used_nodes--;
}

/* ─────────────────────────────────────────────
   Buat lunfs_node baru + vfs_node wrapper-nya
   ───────────────────────────────────────────── */
static lunfs_node_t *lfs_create_node(lunfs_node_t *parent,
                                      const char   *name,
                                      uint32_t      type,
                                      uint32_t      perm)
{
    lunfs_node_t *lnode = lfs_alloc_node();
    if (!lnode) return NULL;

    lfs_strncpy(lnode->name, name, VFS_MAX_NAME);
    lnode->type        = type;
    lnode->permissions = perm;
    lnode->uid         = 0;
    lnode->size        = 0;
    lnode->created_at  = arm_timer_get_uptime_ms();
    lnode->modified_at = lnode->created_at;
    lnode->data        = NULL;
    lnode->data_capacity = 0;
    lnode->parent      = parent;
    lnode->children    = NULL;
    lnode->next_sibling= NULL;

    /* Buat vfs_node wrapper */
    vfs_node_t *vnode = vfs_node_alloc(name, type, perm, &lunfs_ops);
    if (!vnode) { lfs_free_node(lnode); return NULL; }

    vnode->fs_data    = lnode;   /* link ke lunfs_node */
    vnode->inode      = lnode->inode;
    lnode->vnode      = vnode;

    /* Pasang ke parent */
    if (parent) {
        vfs_node_add_child(parent->vnode, vnode);
        lnode->next_sibling  = parent->children;
        parent->children     = lnode;
    }

    return lnode;
}

/* ─────────────────────────────────────────────
   Cari lunfs_node berdasarkan nama di parent
   ───────────────────────────────────────────── */
static lunfs_node_t *lfs_find_child(lunfs_node_t *parent,
                                     const char   *name)
{
    if (!parent) return NULL;
    lunfs_node_t *cur = parent->children;
    while (cur) {
        if (lfs_strcmp(cur->name, name) == 0) return cur;
        cur = cur->next_sibling;
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   Auto-resize data buffer file
   ───────────────────────────────────────────── */
static int lfs_ensure_capacity(lunfs_node_t *node, uint64_t needed) {
    if (needed > LUNFS_MAX_FILE_SIZE) return -1;
    if (needed <= node->data_capacity) return 0;

    /* Double capacity sampai cukup */
    uint64_t new_cap = node->data_capacity == 0 ? 512 : node->data_capacity;
    while (new_cap < needed) new_cap *= 2;
    if (new_cap > LUNFS_MAX_FILE_SIZE) new_cap = LUNFS_MAX_FILE_SIZE;

    uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
    if (!new_data) return -1;

    /* Salin data lama */
    if (node->data && node->size > 0) {
        lfs_memcpy(new_data, node->data, node->size);
    }
    /* Nol-kan sisanya */
    lfs_memset(new_data + node->size, 0, new_cap - node->size);

    if (node->data) kfree(node->data);
    node->data          = new_data;
    node->data_capacity = (uint32_t)new_cap;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
   VFS OPERATIONS — diisi ke lunfs_ops
   ═══════════════════════════════════════════════════════════ */

/* ── open ── */
static int lunfs_op_open(vfs_node_t *node, uint32_t flags) {
    (void)node; (void)flags;
    return 0;   /* LunarFS tidak butuh state saat open */
}

/* ── close ── */
static int lunfs_op_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

/* ── read ── */
static int64_t lunfs_op_read(vfs_node_t *node, void *buf,
                               uint64_t size, uint64_t offset)
{
    if (!node || !buf) return -1;
    if (node->type != VFS_NODE_FILE) return -1;

    lunfs_node_t *lnode = (lunfs_node_t *)node->fs_data;
    if (!lnode || !lnode->data) return 0;

    if (offset >= lnode->size) return 0;

    uint64_t avail = lnode->size - offset;
    uint64_t n     = (size < avail) ? size : avail;

    lfs_memcpy(buf, lnode->data + offset, n);
    return (int64_t)n;
}

/* ── write ── */
static int64_t lunfs_op_write(vfs_node_t *node, const void *buf,
                                uint64_t size, uint64_t offset)
{
    if (!node || !buf) return -1;
    if (node->type != VFS_NODE_FILE) return -1;

    lunfs_node_t *lnode = (lunfs_node_t *)node->fs_data;
    if (!lnode) return -1;

    uint64_t end = offset + size;
    if (lfs_ensure_capacity(lnode, end) != 0) {
        pl011_puts("[LUNFS] ERROR: kapasitas penuh\n");
        return -1;
    }

    lfs_memcpy(lnode->data + offset, buf, size);

    if (end > lnode->size) {
        lnode->size   = (uint32_t)end;
        node->size    = lnode->size;
    }

    lnode->modified_at = arm_timer_get_uptime_ms();
    node->modified_at  = lnode->modified_at;

    return (int64_t)size;
}

/* ── mkdir ── */
static int lunfs_op_mkdir(vfs_node_t *parent_vnode,
                           const char *name, uint32_t perm)
{
    if (!parent_vnode || !name) return -1;

    lunfs_node_t *parent = (lunfs_node_t *)parent_vnode->fs_data;
    if (!parent) return -1;

    /* Cek duplikat */
    if (lfs_find_child(parent, name)) {
        pl011_printf("[LUNFS] mkdir: '%s' sudah ada\n", name);
        return -1;
    }

    lunfs_node_t *dir = lfs_create_node(parent, name,
                                         VFS_NODE_DIR, perm);
    if (!dir) return -1;

    pl011_printf("[LUNFS] mkdir: %s\n", name);
    return 0;
}

/* ── unlink ── */
static int lunfs_op_unlink(vfs_node_t *parent_vnode, const char *name) {
    if (!parent_vnode || !name) return -1;

    lunfs_node_t *parent = (lunfs_node_t *)parent_vnode->fs_data;
    if (!parent) return -1;

    /* Cari node yang akan dihapus */
    lunfs_node_t *prev = NULL;
    lunfs_node_t *cur  = parent->children;

    while (cur) {
        if (lfs_strcmp(cur->name, name) == 0) break;
        prev = cur;
        cur  = cur->next_sibling;
    }

    if (!cur) return -1;

    /* Tidak bisa hapus direktori yang tidak kosong */
    if (cur->type == VFS_NODE_DIR && cur->children) {
        pl011_puts("[LUNFS] unlink: direktori tidak kosong\n");
        return -1;
    }

    /* Lepas dari linked list */
    if (prev) prev->next_sibling  = cur->next_sibling;
    else      parent->children    = cur->next_sibling;

    pl011_printf("[LUNFS] unlink: %s\n", name);
    lfs_free_node(cur);
    return 0;
}

/* ── finddir ── */
static vfs_node_t *lunfs_op_finddir(vfs_node_t *dir_vnode,
                                     const char *name)
{
    if (!dir_vnode || !name) return NULL;

    lunfs_node_t *ldir = (lunfs_node_t *)dir_vnode->fs_data;
    if (!ldir) return NULL;

    lunfs_node_t *child = lfs_find_child(ldir, name);
    if (!child) return NULL;

    return child->vnode;
}

/* ── readdir ── */
static int lunfs_op_readdir(vfs_node_t *dir_vnode, uint32_t index,
                             char *name_out)
{
    if (!dir_vnode || !name_out) return -1;

    lunfs_node_t *ldir = (lunfs_node_t *)dir_vnode->fs_data;
    if (!ldir) return -1;

    lunfs_node_t *cur = ldir->children;
    uint32_t i = 0;

    while (cur) {
        if (i == index) {
            lfs_strncpy(name_out, cur->name, VFS_MAX_NAME);
            return 0;
        }
        i++;
        cur = cur->next_sibling;
    }

    return -1;   /* index melebihi jumlah anak */
}

/* ── stat ── */
static int lunfs_op_stat(vfs_node_t *node, vfs_stat_t *out) {
    if (!node || !out) return -1;

    lunfs_node_t *lnode = (lunfs_node_t *)node->fs_data;
    if (!lnode) return -1;

    out->type        = lnode->type;
    out->permissions = lnode->permissions;
    out->uid         = lnode->uid;
    out->size        = lnode->size;
    out->created_at  = lnode->created_at;
    out->modified_at = lnode->modified_at;
    return 0;
}

/* ── sync ── */
static int lunfs_op_sync(vfs_node_t *node) {
    (void)node;
    /*
     * LunarFS in-memory — tidak ada yang perlu di-flush ke disk.
     * Kalau nanti ada persistensi, tulis ke storage di sini.
     */
    return 0;
}

/* ─────────────────────────────────────────────
   lunfs_ops — tabel operasi yang didaftarkan ke VFS
   ───────────────────────────────────────────── */
vfs_ops_t lunfs_ops = {
    .open    = lunfs_op_open,
    .close   = lunfs_op_close,
    .read    = lunfs_op_read,
    .write   = lunfs_op_write,
    .mkdir   = lunfs_op_mkdir,
    .unlink  = lunfs_op_unlink,
    .finddir = lunfs_op_finddir,
    .readdir = lunfs_op_readdir,
    .stat    = lunfs_op_stat,
    .sync    = lunfs_op_sync,
};

/* ─────────────────────────────────────────────
   lunfs_mount()
   Inisialisasi LunarFS dan kembalikan root node
   ───────────────────────────────────────────── */
int lunfs_mount(vfs_node_t **root_out) {
    if (lunfs_mounted) return -1;

    /* Reset node pool */
    lfs_memset(node_pool, 0, sizeof(node_pool));
    next_inode = 1;

    /* Setup superblock */
    superblock.magic       = LUNFS_MAGIC;
    superblock.version     = LUNFS_VERSION;
    superblock.total_nodes = LUNFS_MAX_NODES;
    superblock.used_nodes  = 0;
    superblock.created_at  = arm_timer_get_uptime_ms();

    /* Buat root node "/" */
    lunfs_node_t *root_lnode = lfs_alloc_node();
    if (!root_lnode) return -1;

    lfs_strncpy(root_lnode->name, "/", VFS_MAX_NAME);
    root_lnode->type        = VFS_NODE_DIR;
    root_lnode->permissions = PERM_OWNER_RWX | PERM_GROUP_RX | PERM_OTHER_RX;
    root_lnode->uid         = 0;
    root_lnode->created_at  = superblock.created_at;
    root_lnode->modified_at = superblock.created_at;
    root_lnode->parent      = NULL;
    root_lnode->children    = NULL;

    /* Buat vfs_node untuk root */
    vfs_node_t *root_vnode = vfs_node_alloc("/", VFS_NODE_DIR,
                                              root_lnode->permissions,
                                              &lunfs_ops);
    if (!root_vnode) {
        lfs_free_node(root_lnode);
        return -1;
    }

    root_vnode->fs_data = root_lnode;
    root_vnode->inode   = root_lnode->inode;
    root_lnode->vnode   = root_vnode;

    lunfs_mounted = 1;
    *root_out     = root_vnode;

    pl011_printf("[LUNFS] Mounted: max_nodes=%d block_size=%d\n",
                 LUNFS_MAX_NODES, LUNFS_BLOCK_SIZE);
    return 0;
}

/* ─────────────────────────────────────────────
   lunfs_dump_tree() — debug: tampilkan semua node
   ───────────────────────────────────────────── */
static void dump_node(lunfs_node_t *node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth * 2; i++) pl011_putc(' ');

    pl011_puts(node->type == VFS_NODE_DIR ? "[D] " : "[F] ");
    pl011_puts(node->name);

    if (node->type == VFS_NODE_FILE) {
        pl011_puts(" (");
        pl011_print_uint(node->size);
        pl011_puts(" B)");
    }
    pl011_puts("\n");

    /* Rekursif ke children */
    lunfs_node_t *child = node->children;
    while (child) {
        dump_node(child, depth + 1);
        child = child->next_sibling;
    }
}

void lunfs_dump_tree(void) {
    pl011_puts("\n[LUNFS] Filesystem tree:\n");
    pl011_puts("─────────────────────────\n");

    for (int i = 0; i < LUNFS_MAX_NODES; i++) {
        if (node_pool[i].used && node_pool[i].parent == NULL) {
            dump_node(&node_pool[i], 0);
        }
    }

    pl011_printf("\nUsed nodes: %u / %u\n",
                 superblock.used_nodes,
                 superblock.total_nodes);
    pl011_puts("─────────────────────────\n");
}
