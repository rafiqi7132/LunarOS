// Sebelum kernel di-load, verifikasi signature-nya
// Mirip Secure Boot di iOS/UEFI
int secure_boot_verify_kernel(uint8_t *kernel_image, uint32_t size,
                               uint8_t *expected_hash) {
    uint8_t actual_hash[32];
    sha256(kernel_image, size, actual_hash);
    return memcmp(actual_hash, expected_hash, 32) == 0;
}