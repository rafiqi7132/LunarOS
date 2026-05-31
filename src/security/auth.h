#ifndef AUTH_H
#define AUTH_H

#define MAX_USERS     8
#define MAX_USERNAME  32
#define HASH_SIZE     32   // SHA-256

typedef enum {
    ROLE_GUEST   = 0,
    ROLE_USER    = 1,
    ROLE_ADMIN   = 2,
    ROLE_KERNEL  = 3      // hanya proses kernel
} user_role_t;

typedef struct {
    char     username[MAX_USERNAME];
    uint8_t  password_hash[HASH_SIZE];  // simpan hash, BUKAN plaintext
    user_role_t role;
    int      active;
    uint32_t failed_attempts;
    uint32_t lockout_until;             // timestamp lockout
} user_t;

int  auth_init(void);
int  auth_login(const char *username, const char *password);
void auth_logout(void);
int  auth_check_permission(user_role_t required_role);
user_t *auth_current_user(void);

#endif