/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Nintendo Switch: newlib declares flock() but the console has no
   implementation; switch/src/switch_shims.c supplies one that always
   succeeds, since only one process ever opens the save journal. This
   wrapper only guarantees the LOCK_* constants. */
#ifndef DOA3_NX_SYS_FILE_H
#define DOA3_NX_SYS_FILE_H

#include_next <sys/file.h>

#ifndef LOCK_SH
#define LOCK_SH 0x01
#define LOCK_EX 0x02
#define LOCK_NB 0x04
#define LOCK_UN 0x08
#endif

#ifdef __cplusplus
extern "C" {
#endif
int flock(int fd, int operation);
#ifdef __cplusplus
}
#endif

#endif
