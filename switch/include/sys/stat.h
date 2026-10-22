/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Nintendo Switch: newlib only defines the futimens() special values on
   Cygwin and RTEMS. */
#ifndef DOA3_NX_SYS_STAT_H
#define DOA3_NX_SYS_STAT_H

#include_next <sys/stat.h>

#ifndef UTIME_NOW
#define UTIME_NOW -2L
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT -1L
#endif

#endif
