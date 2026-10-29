/* SPDX-License-Identifier: GPL-3.0-or-later
   Force-included into the generated chunks of the Switch build: direct calls
   to the math helpers in switch/src/doa3_nx_math.c take the native version.
   The guest address is a constant at every call site, so the compiler folds
   the test away. */
#ifndef DOA3_NX_NATIVE_H
#define DOA3_NX_NATIVE_H

#include <stdint.h>

typedef void (*Doa3NxNativeFunction)(void);

void doa3_nx_native_00154750(void);
void doa3_nx_native_001547E0(void);
void doa3_nx_native_001548E0(void);
void doa3_nx_native_00154570(void);
void doa3_nx_native_00154680(void);
Doa3NxNativeFunction doa3_nx_native_lookup(uint32_t guest_address);

#ifndef DOA3_NX_NATIVE_OFF
#define RECOMP_ABI_CALL(va, fn) ( \
    (va) == 0x00154750u ? doa3_nx_native_00154750() : \
    (va) == 0x001547E0u ? doa3_nx_native_001547E0() : \
    (va) == 0x001548E0u ? doa3_nx_native_001548E0() : \
    (va) == 0x00154570u ? doa3_nx_native_00154570() : \
    (va) == 0x00154680u ? doa3_nx_native_00154680() : \
    (fn)())
#endif

#endif
