/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Weak fallbacks for POSIX calls the runtime links against but devkitA64's
   newlib/libsysbase may not provide. A strong definition in the toolchain
   libraries wins whenever it exists. */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

__attribute__((weak)) long sysconf(int name)
{
    if (name == _SC_PAGESIZE) return 0x1000;
    errno = EINVAL;
    return -1;
}

/* File times are cosmetic for the guest; the SD card keeps its own. */
__attribute__((weak)) int futimens(int fd, const struct timespec times[2])
{
    (void)fd;
    (void)times;
    return 0;
}

/* C11 timespec_get: newlib declares it, devkitA64 does not implement it. */
__attribute__((weak)) int timespec_get(struct timespec *ts, int base)
{
    if (ts == NULL || base != TIME_UTC) return 0;
    if (clock_gettime(CLOCK_REALTIME, ts) != 0) return 0;
    return base;
}

/* One process owns the SD card's save journal; the advisory lock always holds. */
__attribute__((weak)) int flock(int fd, int operation)
{
    (void)operation;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

__attribute__((weak)) int fchmod(int fd, mode_t mode)
{
    (void)fd;
    (void)mode;
    return 0;
}
