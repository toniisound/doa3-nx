#include "cri_adxm_adapter.h"
#include "doa3_build.h"
#include "kernel_abi.h"
#include "cri_service_model.h"
#include "d3d_vblank.h"
#include "xapi_time_adapter.h"

#include <stddef.h>

/* DOA3 addresses. ADXM_SetupThrd creates four threads: an idle spinner
   (0x0016A530), two vertical-blank threads (0x0016A570, 0x0016A5E0) and the
   main server (0x0016A650). One pass of each loop body: */
enum {
    VSYNC_SERVER_A = DOA3(0x00170640u, 0x0019a640u), /* first vertical-blank lane */
    VSYNC_SERVER_B = DOA3(0x00170660u, 0x0019a660u), /* second vertical-blank lane */
    MAIN_SERVER = DOA3(0x00170690u, 0x0019a690u),    /* nonzero while work remains */
    MAIN_WAKE_FLAG = DOA3(0x00b24d3cu, 0x0040c6fcu), /* 1 ends a main-server batch */
    USER_CALLBACK = DOA3(0x00b24d50u, 0x0040c710u),  /* optional (fn, arg) after each pass */
    USER_CALLBACK_ARG = DOA3(0x00b24d54u, 0x0040c714u),
    VBLANK_COUNT = DOA3(0x00b24d44u, 0x0040c704u),
    MAIN_PASS_COUNT = DOA3(0x00b24d48u, 0x0040c708u),
    MAIN_THREAD = DOA3(0x00c0c508u, 0x00c80078u),
    MAIN_STOP = DOA3(0x00b24d70u, 0x0040c730u),      /* MAIN_STOP + 4 acknowledges */
    MAIN_PRIORITY = DOA3(0x00b24d34u, 0x0040c6f4u),
    VBLANK_A_STOP = DOA3(0x00b24d60u, 0x0040c720u),
    VBLANK_B_STOP = DOA3(0x00b24d68u, 0x0040c728u),
    /* ponytail: bounds a batch the guest never ends; raise it if a
       caller still sees pending work after one step. */
    MAIN_SERVER_LIMIT = 64u,
};

void recomp_cri_adxm_server_step(void)
{
    RecompRegisters saved = recomp_runtime.registers;
    uint32_t callback;

    /* One pass reads at most 1 MB. Repeat while passes read, so the file a
       caller polls finishes within this poll instead of a chunk per frame.
       shortcut: 16 rounds covers 16 MB per poll; larger reads resume on the
       next poll. */
    for (unsigned round = 0u; round < 16u; ++round) {
        uint32_t reads = recomp_kernel_read_count();
        kernel_call_guest(VSYNC_SERVER_A, NULL, 0u);
        kernel_call_guest(VSYNC_SERVER_B, NULL, 0u);
        for (unsigned i = 0u; i < MAIN_SERVER_LIMIT; ++i) {
            kernel_call_guest(MAIN_SERVER, NULL, 0u);
            if (recomp_runtime.registers.eax == 0u ||
                *recomp_memory_u32(MAIN_WAKE_FLAG) == 1u) {
                break;
            }
        }
        if (recomp_kernel_read_count() == reads) break;
    }
    callback = *recomp_memory_u32(USER_CALLBACK);
    if (callback != 0u) {
        uint32_t argument = *recomp_memory_u32(USER_CALLBACK_ARG);
        kernel_call_guest(callback, &argument, 1u);
    }
    recomp_runtime.registers = saved;
}

/* The cooperative host can resume after several refreshes. Keep middleware
   time on the 60 Hz clock even when decoding or rendering misses a refresh. */
static void vblank_thread(uint32_t server, uint32_t stop)
{
    uint64_t tick = recomp_cri_vblank_tick(recomp_xapi_performance_counter());
    while (*recomp_memory_u32(stop) == 0u) {
        recomp_d3d_wait_vblank();
        uint64_t due = recomp_cri_vblank_tick(recomp_xapi_performance_counter());
        while (tick < due) {
            ++tick;
            ++*recomp_memory_u32(VBLANK_COUNT);
            kernel_call_guest(server, NULL, 0u);
        }
        uint32_t main_thread = *recomp_memory_u32(MAIN_THREAD);
        kernel_call_guest(0x800000e0u, (uint32_t[]){main_thread, 0u}, 2u);
        uint32_t callback = *recomp_memory_u32(USER_CALLBACK);
        if (callback != 0u) {
            uint32_t argument = *recomp_memory_u32(USER_CALLBACK_ARG);
            kernel_call_guest(callback, &argument, 1u);
        }
    }
    *recomp_memory_u32(stop + 4u) = 1u;
    const uint32_t status = 0xf0000001u;
    kernel_call_guest(0x80000102u, &status, 1u);
}

void recomp_cri_adxm_vblank_a_thread(void) { vblank_thread(VSYNC_SERVER_A, VBLANK_A_STOP); }
void recomp_cri_adxm_vblank_b_thread(void) { vblank_thread(VSYNC_SERVER_B, VBLANK_B_STOP); }

/* The middleware worker owns a batch until the server drains or the caller
   requests an acknowledgement. Its wait is a real guest self-suspension. */
void recomp_cri_adxm_main_thread(void)
{
    const uint32_t self = 0xfffffffeu;
    const uint32_t suspend_args[2] = {self, 0u};
    while (*recomp_memory_u32(MAIN_STOP) == 0u) {
        ++*recomp_memory_u32(MAIN_PASS_COUNT);
        kernel_call_guest(MAIN_SERVER, NULL, 0u);
        if (recomp_runtime.registers.eax != 0u &&
            *recomp_memory_u32(MAIN_WAKE_FLAG) != 1u) continue;
        if (*recomp_memory_u32(MAIN_WAKE_FLAG) == 1u) {
            uint32_t priority = *recomp_memory_u32(MAIN_PRIORITY);
            /* Match XAPI's idle/time-critical saturation conversion. */
            if (priority == 15u) priority = 16u;
            if (priority == (uint32_t)-15) priority = (uint32_t)-16;
            const uint32_t args[2] = {self, priority};
            *recomp_memory_u32(MAIN_WAKE_FLAG) = 0u;
            kernel_call_guest(0x8000008fu, args, 2u); /* KeSetBasePriorityThread */
        }
        uint32_t callback = *recomp_memory_u32(USER_CALLBACK);
        if (callback != 0u) {
            uint32_t argument = *recomp_memory_u32(USER_CALLBACK_ARG);
            kernel_call_guest(callback, &argument, 1u);
        }
        kernel_call_guest(0x800000e7u, suspend_args, 2u); /* NtSuspendThread */
    }
    *recomp_memory_u32(MAIN_STOP + 4u) = 1u;
    const uint32_t status = 0xf0000001u;
    kernel_call_guest(0x80000102u, &status, 1u); /* PsTerminateSystemThread */
}
