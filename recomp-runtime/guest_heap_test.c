#include "xbox_memory_layout.h"

#include <stdio.h>

int main(void)
{
    /* Live virtual allocations, including thread stacks, must coexist with
       contiguous resources without reserving a second startup stack. */
    uint32_t heap = xbox_HeapAlloc(0x500000u, 0x1000u);
    uint32_t resource = xbox_ContiguousAlloc(
        0x2a80000u, 0u, UINT32_MAX, 0x1000u);
    uint32_t physical = resource & 0x7fffffffu;
    uint32_t tls_page = XBOX_STARTUP_THREAD_STACK_SLOT & ~0xfffu;
    if (heap == 0u || resource == 0u || physical < heap + 0x500000u ||
        physical + 0x2a80000u != tls_page) {
        fprintf(stderr, "guest heap: live allocations lost to obsolete stack reservation\n");
        return 1;
    }
    uint32_t cursor = xbox_HeapCheckpoint();
    if (xbox_HeapAlloc(physical - cursor, 4u) != cursor ||
        xbox_HeapAlloc(4u, 4u) != 0u ||
        xbox_ContiguousAlloc(0x1000u, 0u, UINT32_MAX, 0x1000u) != 0u ||
        xbox_HeapCheckpoint() != physical) {
        fprintf(stderr, "guest heap: overlapping allocations accepted\n");
        return 1;
    }
    /* Exhausting the high arena must not hide the page-aligned gap between
       the image and kernel data. Neither boundary may be crossed. */
    uint32_t image_end = XBOX_KERNEL_DATA_BASE - 0x20000u + 1u;
    if (xbox_HeapSetImageEnd(XBOX_KERNEL_DATA_BASE + 1u) ||
        !xbox_HeapSetImageEnd(image_end) || xbox_HeapSetImageEnd(image_end) ||
        xbox_ContiguousAlloc(0x10000u, 0x1000000u, UINT32_MAX, 0x1000u) != 0u ||
        xbox_ContiguousAlloc(0x10000u, 0u, UINT32_MAX, 0x1000u) !=
            (0x80000000u | (XBOX_KERNEL_DATA_BASE - 0x10000u)) ||
        /* A full heap continues in the gap too. */
        xbox_HeapAlloc(0x1000u, 0x1000u) != XBOX_KERNEL_DATA_BASE - 0x11000u ||
        xbox_ContiguousAlloc(0x10000u, 0u, UINT32_MAX, 0x1000u) != 0u ||
        xbox_ContiguousAlloc(0xe000u, 0u, XBOX_KERNEL_DATA_BASE - 0x10001u, 0x1000u) !=
            (0x80000000u | (XBOX_KERNEL_DATA_BASE - 0x1f000u)) ||
        xbox_ContiguousAlloc(1u, 0u, UINT32_MAX, 0x1000u) != 0u) {
        fprintf(stderr, "guest heap: image-gap allocation crossed a live boundary\n");
        return 1;
    }
    return 0;
}
