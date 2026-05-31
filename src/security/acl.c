// Setiap resource (file, device, memori) punya ACL
typedef struct {
    char     resource_path[128];
    uint32_t owner_uid;
    uint8_t  perms_owner;   // bitmask: READ=4, WRITE=2, EXEC=1
    uint8_t  perms_group;
    uint8_t  perms_other;
} acl_entry_t;

// Cek apakah user boleh akses resource
int acl_check(const char *path, uint32_t uid, uint8_t required_perm);
int acl_set(const char *path, uint32_t uid, uint8_t owner_p, uint8_t group_p, uint8_t other_p);
