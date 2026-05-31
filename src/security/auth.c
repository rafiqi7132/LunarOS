#include "auth.h"
#include "crypto.h"
#include "../lib/string.h"

#define MAX_FAILED_ATTEMPTS  5
#define LOCKOUT_DURATION     300   // 5 menit (dalam detik/tick)

static user_t  users[MAX_USERS];
static user_t *current_user = NULL;
static int     user_count   = 0;

// Hash password pakai SHA-256 sebelum disimpan
int auth_add_user(const char *username, const char *password, user_role_t role) {
    if (user_count >= MAX_USERS) return -1;
    user_t *u = &users[user_count];
    strncpy(u->username, username, MAX_USERNAME - 1);
    sha256((uint8_t*)password, strlen(password), u->password_hash);
    u->role            = role;
    u->active          = 1;
    u->failed_attempts = 0;
    u->lockout_until   = 0;
    user_count++;
    return 0;
}

int auth_login(const char *username, const char *password) {
    for (int i = 0; i < user_count; i++) {
        user_t *u = &users[i];
        if (!u->active) continue;
        if (strncmp(u->username, username, MAX_USERNAME) != 0) continue;

        // Cek lockout
        if (u->lockout_until > get_system_tick()) {
            return -2;  // akun terkunci
        }

        // Verifikasi password via hash
        uint8_t input_hash[HASH_SIZE];
        sha256((uint8_t*)password, strlen(password), input_hash);

        if (memcmp(input_hash, u->password_hash, HASH_SIZE) == 0) {
            u->failed_attempts = 0;
            current_user = u;
            return 0;  // sukses
        } else {
            u->failed_attempts++;
            if (u->failed_attempts >= MAX_FAILED_ATTEMPTS) {
                u->lockout_until = get_system_tick() + LOCKOUT_DURATION;
            }
            return -1;  // password salah
        }
    }
    return -1;
}

int auth_check_permission(user_role_t required_role) {
    if (!current_user) return 0;
    return current_user->role >= required_role;
}