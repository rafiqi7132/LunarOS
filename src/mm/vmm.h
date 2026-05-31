/**
 * vmm.h - LunarOS Virtual Memory Manager Header
 * Lokasi: src/mm/vmm.h
 */

#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "pmm.h"

/* Flag untuk vmm_map_page() — kombinasikan dengan OR */
#define VMM_FLAG_KERNEL  (0x0040000000000600ULL)  /* kernel RW, no-exec user */
#define VMM_FLAG_USER_RW (0x0040000000000640ULL)  /* user RW */
#define VMM_FLAG_USER_RO (0x0040000000000680ULL)  /* user RO */
#define VMM_FLAG_DEVICE  (0x0060000000000604ULL)  /* device MMIO */

int       vmm_init(void);

uint64_t *vmm_create_pgd(void);
void      vmm_destroy_pgd(uint64_t *pgd);
void      vmm_switch_pgd(uint64_t *pgd);
uint64_t *vmm_get_kernel_pgd(void);

void      vmm_map_page(uint64_t *pgd, uint64_t virt,
                        uint64_t phys, uint64_t flags);
void      vmm_unmap_page(uint64_t *pgd, uint64_t virt);

#endif /* VMM_H */