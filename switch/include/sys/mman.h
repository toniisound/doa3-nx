/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Nintendo Switch: newlib has no mmap. native_fiber.c only maps anonymous
   fiber stacks and protects one guard page, so plain page-aligned heap
   memory is enough. There is no guard page: a fiber stack overflow
   corrupts the heap instead of faulting, as on any host without an MMU
   guard. Stacks are 1 MiB, far more than the lifted code uses. */
#ifndef DOA3_NX_SYS_MMAN_H
#define DOA3_NX_SYS_MMAN_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

static inline void *mmap(void *addr, size_t length, int prot, int flags,
    int fd, off_t offset)
{
    void *memory;
    (void)addr; (void)prot; (void)offset;
    if ((flags & MAP_ANONYMOUS) == 0 || fd != -1 || length == 0) {
        return MAP_FAILED;
    }
    length = (length + 0xfffu) & ~(size_t)0xfffu;
    memory = aligned_alloc(0x1000u, length);
    if (memory == NULL) {
        return MAP_FAILED;
    }
    memset(memory, 0, length);
    return memory;
}

static inline int munmap(void *addr, size_t length)
{
    (void)length;
    free(addr);
    return 0;
}

static inline int mprotect(void *addr, size_t length, int prot)
{
    (void)addr; (void)length; (void)prot;
    return 0;
}

#endif
