#include "xbox_memory_layout.h"

enum {
    XBOX_CACHED_ALIAS = 0x80000000u,
    XBOX_HEAP_BASE = 0x01000000u,
    XBOX_HEAP_LIMIT = 0x03800000u,
    /* Scheduled threads own heap stacks. Only bootstrap TLS remains at the
       old inline startup stack top; protect its page, not the old 1 MiB stack. */
    XBOX_CONTIGUOUS_LIMIT = XBOX_STARTUP_THREAD_STACK_SLOT & ~0xfffu,
    XBOX_PAGE_SIZE = 0x1000u,
};

static uint32_t heap_cursor = XBOX_HEAP_BASE;
static uint32_t contiguous_cursor = XBOX_CONTIGUOUS_LIMIT;
static uint32_t image_gap_floor = XBOX_KERNEL_DATA_BASE;
static uint32_t image_gap_cursor = XBOX_KERNEL_DATA_BASE;

bool xbox_HeapSetImageEnd(uint32_t image_end)
{
    if (image_end == 0u || image_end > XBOX_KERNEL_DATA_BASE ||
        image_gap_floor != XBOX_KERNEL_DATA_BASE) return false;
    image_gap_floor = (image_end + XBOX_PAGE_SIZE - 1u) & ~(XBOX_PAGE_SIZE - 1u);
    return true;
}

/* Top-down from the page-aligned free RAM after the XBE, below kernel data. */
static uint32_t image_gap_alloc(
    uint64_t size, uint64_t upper_bound, uint32_t lowest_address, uint32_t alignment)
{
    uint64_t base;

    if (upper_bound > image_gap_cursor) upper_bound = image_gap_cursor;
    if (size > upper_bound) return 0u;
    base = (upper_bound - size) & ~(uint64_t)(alignment - 1u);
    if (base < lowest_address || base < image_gap_floor) return 0u;
    image_gap_cursor = (uint32_t)base;
    return (uint32_t)base;
}

uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment)
{
    uint64_t aligned;
    uint64_t end;

    if (alignment < 4u || (alignment & (alignment - 1u)) != 0u) {
        return 0u;
    }
    aligned = ((uint64_t)heap_cursor + alignment - 1u) & ~(uint64_t)(alignment - 1u);
    end = aligned + size;
    if (end > XBOX_HEAP_LIMIT || end > contiguous_cursor) {
        /* Contiguous memory can reach the heap first; whichever arena fills
           last may use the gap. shortcut: xbox_HeapRestore does not reclaim
           gap allocations, which only failed-creation rollbacks would need. */
        return image_gap_alloc(size, XBOX_KERNEL_DATA_BASE, 0u, alignment);
    }
    heap_cursor = (uint32_t)end;
    return (uint32_t)aligned;
}

void xbox_HeapFree(uint32_t guest_address)
{
    (void)guest_address;
}

uint32_t xbox_HeapCheckpoint(void)
{
    return heap_cursor;
}

bool xbox_HeapRestore(uint32_t checkpoint)
{
    if (checkpoint < XBOX_HEAP_BASE || checkpoint > heap_cursor) {
        return false;
    }
    heap_cursor = checkpoint;
    return true;
}

uint32_t xbox_ContiguousAlloc(
    uint32_t size,
    uint32_t lowest_address,
    uint32_t highest_address,
    uint32_t alignment)
{
    uint64_t rounded_size;
    uint64_t upper_bound;
    uint64_t base;

    if (size == 0u) {
        return 0u;
    }
    if (alignment < XBOX_PAGE_SIZE) {
        alignment = XBOX_PAGE_SIZE;
    }
    if ((alignment & (alignment - 1u)) != 0u) {
        return 0u;
    }
    rounded_size = ((uint64_t)size + XBOX_PAGE_SIZE - 1u) &
        ~(uint64_t)(XBOX_PAGE_SIZE - 1u);
    upper_bound = highest_address == UINT32_MAX
        ? XBOX_CONTIGUOUS_LIMIT
        : (uint64_t)highest_address + 1u;
    if (upper_bound > contiguous_cursor) {
        upper_bound = contiguous_cursor;
    }
    if (rounded_size > upper_bound) {
        return 0u;
    }
    base = (upper_bound - rounded_size) & ~(uint64_t)(alignment - 1u);
    if (base < lowest_address || base < heap_cursor) {
        /* The fixed 16 MiB heap base must not hide free RAM after the XBE.
           Keep the separate kernel data block outside both allocation arenas. */
        base = image_gap_alloc(rounded_size, (uint64_t)highest_address + 1u,
            lowest_address, alignment);
        if (base == 0u) return 0u;
    } else {
        contiguous_cursor = (uint32_t)base;
    }
    return XBOX_CACHED_ALIAS | (uint32_t)base;
}
