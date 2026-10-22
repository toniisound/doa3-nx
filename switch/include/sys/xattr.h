/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Nintendo Switch: the SD card filesystem has no extended attributes.
   win32_compat.c stores Windows file attributes in a user xattr on Linux;
   here every call reports "not supported", so files keep their POSIX
   attributes only (read-only from the mode bits). */
#ifndef DOA3_NX_SYS_XATTR_H
#define DOA3_NX_SYS_XATTR_H

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef ENOTSUP
#define ENOTSUP 134
#endif

static inline ssize_t getxattr(const char *path, const char *name,
    void *value, size_t size)
{
    (void)path; (void)name; (void)value; (void)size;
    errno = ENOTSUP;
    return -1;
}

static inline ssize_t fgetxattr(int fd, const char *name, void *value, size_t size)
{
    (void)fd; (void)name; (void)value; (void)size;
    errno = ENOTSUP;
    return -1;
}

static inline int setxattr(const char *path, const char *name,
    const void *value, size_t size, int flags)
{
    (void)path; (void)name; (void)value; (void)size; (void)flags;
    errno = ENOTSUP;
    return -1;
}

static inline int fsetxattr(int fd, const char *name,
    const void *value, size_t size, int flags)
{
    (void)fd; (void)name; (void)value; (void)size; (void)flags;
    errno = ENOTSUP;
    return -1;
}

#endif
