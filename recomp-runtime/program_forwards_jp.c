/* Japan 3.1 (XDK 4134), generated from program_forwards.c with the
   USA-to-Japan Version Tracking map; see docs/doa3-jp-test.md.
   shortcut: a committed copy for the JP boot test; generate it in the
   build if JP becomes a real target. */
/* Library entry points rebound to hand-written adapters.

   The lifter generates no body for an address listed in
   tools/doa3/manual_functions.json and emits every call to it as indirect
   dispatch, which tries recomp_lookup_manual() first. The dispatch table and
   recomp_funcs.h still name sub_X, so define it here as one
   recomp_program_manual_call() call, e.g.
     void sub_001DF010(void) { recomp_program_manual_call(0x001df010u); }
   It runs only when no adapter is bound, and then stops naming the address. */
#include "program_manual.h"
#include "stop_report.h"
#include "cri_adxm_adapter.h"
#include "kernel_abi.h"
#include "free_camera_adapter.h"

#include <inttypes.h>
#include <stddef.h>

void recomp_program_manual_call(uint32_t guest_address)
{
    RecompFunction function = recomp_lookup_manual(guest_address);

    if (function == NULL) {
        recomp_stop(2, "manual-unbound:0x%08" PRIx32, guest_address);
        return;
    }
    function();
}

/* FIBER: DOA3 XAPILIB 3911; adapters in fiber_adapter.c. */
void sub_0018C934(void) { recomp_program_manual_call(0x0018c934u); }
void sub_0018C9C0(void) { recomp_program_manual_call(0x0018c9c0u); }
void sub_0018C9D3(void) { recomp_program_manual_call(0x0018c9d3u); }
void sub_0018CA12(void) { recomp_program_manual_call(0x0018ca12u); }
/* FIBER end. */

/* D3DDevice_SetGammaRamp, D3DDevice_Clear, D3DDevice_Present (XDK 3925).
   Their adapters are in d3d_frame_adapter.c; see docs/doa3-d3d-symbols.md. */
void sub_001DC360(void) { recomp_program_manual_call(0x001dc360u); }
void sub_001DF010(void) { recomp_program_manual_call(0x001df010u); }
void sub_001DC660(void) { recomp_program_manual_call(0x001dc660u); }
void sub_001E14D0(void) { recomp_program_manual_call(0x001e14d0u); }

/* D3D8 4134 draw, texture, vertex shader and render-state setters; adapters
   in d3d_draw_adapter.c, d3d_texture_adapter.c, d3d_vertex_shader_adapter.c
   and d3d_render_state_adapter.c. */
void sub_001DD670(void) { recomp_program_manual_call(0x001dd670u); }
void sub_001DD880(void) { recomp_program_manual_call(0x001dd880u); }
void sub_001DDEB0(void) { recomp_program_manual_call(0x001ddeb0u); }
void sub_001DE080(void) { recomp_program_manual_call(0x001de080u); }
void sub_001DE150(void) { recomp_program_manual_call(0x001de150u); }
void sub_001DE200(void) { recomp_program_manual_call(0x001de200u); }
void sub_001DE240(void) { recomp_program_manual_call(0x001de240u); }
void sub_001DE470(void) { recomp_program_manual_call(0x001de470u); }
void sub_001DEC40(void) { recomp_program_manual_call(0x001dec40u); }
void sub_001DECE0(void) { recomp_program_manual_call(0x001dece0u); }
void sub_001DED70(void) { recomp_program_manual_call(0x001ded70u); }
void sub_001DEF50(void) { recomp_program_manual_call(0x001def50u); }
void sub_001DF490(void) { recomp_program_manual_call(0x001df490u); }
void sub_001DF5F0(void) { recomp_program_manual_call(0x001df5f0u); }
void sub_001DF690(void) { recomp_program_manual_call(0x001df690u); }
void sub_001DFF50(void) { recomp_program_manual_call(0x001dff50u); }
void sub_001E0310(void) { recomp_program_manual_call(0x001e0310u); }

/* XInitDevices and its jmp thunk, XGetDevices, XGetDeviceChanges, XInputOpen,
   XInputClose, XInputGetCapabilities, XInputGetState, XInputSetState
   (XAPILIB 3911, XPP section). Adapters are in input_adapter.c. */
void sub_00210B6C(void) { recomp_program_manual_call(0x00210b6cu); }
void sub_00211578(void) { recomp_program_manual_call(0x00211578u); }
void sub_0021157D(void) { recomp_program_manual_call(0x0021157du); }
void sub_0021159F(void) { recomp_program_manual_call(0x0021159fu); }
void sub_00211823(void) { recomp_program_manual_call(0x00211823u); }
void sub_00211898(void) { recomp_program_manual_call(0x00211898u); }
void sub_002118A4(void) { recomp_program_manual_call(0x002118a4u); }
void sub_00211A96(void) { recomp_program_manual_call(0x00211a96u); }
void sub_00211B07(void) { recomp_program_manual_call(0x00211b07u); }

/* CDevice::KickOff and the miniport GPU layer under CDevice::Init (D3D8 3925).
   Adapters are in d3d_miniport_adapter.c. */
void sub_001E27C0(void) { recomp_program_manual_call(0x001e27c0u); }
void sub_001E57ED(void) { recomp_program_manual_call(0x001e57edu); }
void sub_001E5283(void) { recomp_program_manual_call(0x001e5283u); }
void sub_001E52E6(void) { recomp_program_manual_call(0x001e52e6u); }
void sub_001E5623(void) { recomp_program_manual_call(0x001e5623u); }
void sub_001E56F2(void) { recomp_program_manual_call(0x001e56f2u); }
void sub_001E4758(void) { recomp_program_manual_call(0x001e4758u); }
void sub_001E594F(void) { recomp_program_manual_call(0x001e594fu); }
void sub_001E5B3D(void) { recomp_program_manual_call(0x001e5b3du); }
void sub_001E2790(void) { recomp_program_manual_call(0x001e2790u); }

/* DirectSound 3936 wrappers game code calls (dsound_api_adapter.c). */
void sub_001F2E65(void) { recomp_program_manual_call(0x001f2e65u); }
void sub_001F1F01(void) { recomp_program_manual_call(0x001f1f01u); }
void sub_001F1D46(void) { recomp_program_manual_call(0x001f1d46u); }
void sub_001F1D62(void) { recomp_program_manual_call(0x001f1d62u); }
void sub_001F29BD(void) { recomp_program_manual_call(0x001f29bdu); }
void sub_001F29F2(void) { recomp_program_manual_call(0x001f29f2u); }
void sub_001F23CC(void) { recomp_program_manual_call(0x001f23ccu); }
void sub_001F2A27(void) { recomp_program_manual_call(0x001f2a27u); }
void sub_001F2CAB(void) { recomp_program_manual_call(0x001f2cabu); }
void sub_001F13AF(void) { recomp_program_manual_call(0x001f13afu); }
void sub_001F1D89(void) { recomp_program_manual_call(0x001f1d89u); }
void sub_001F1DDD(void) { recomp_program_manual_call(0x001f1dddu); }
void sub_001F2400(void) { recomp_program_manual_call(0x001f2400u); }
void sub_001F2424(void) { recomp_program_manual_call(0x001f2424u); }
void sub_001F2459(void) { recomp_program_manual_call(0x001f2459u); }
void sub_001F1E35(void) { recomp_program_manual_call(0x001f1e35u); }
void sub_001F1E59(void) { recomp_program_manual_call(0x001f1e59u); }
void sub_001F1E79(void) { recomp_program_manual_call(0x001f1e79u); }
void sub_001F1E95(void) { recomp_program_manual_call(0x001f1e95u); }
void sub_001F1EB5(void) { recomp_program_manual_call(0x001f1eb5u); }
void sub_001F1ED1(void) { recomp_program_manual_call(0x001f1ed1u); }
void sub_001F23E4(void) { recomp_program_manual_call(0x001f23e4u); }
void sub_001F1DC1(void) { recomp_program_manual_call(0x001f1dc1u); }
void sub_001F1DF9(void) { recomp_program_manual_call(0x001f1df9u); }
void sub_001F1E1D(void) { recomp_program_manual_call(0x001f1e1du); }
void sub_001F2A47(void) { recomp_program_manual_call(0x001f2a47u); }

void sub_001927D0(void) { recomp_program_manual_call(0x001927d0u); }

/* CRI ADXF_GetPtStat (0x00169150) and ADXF_GetStat (0x00168DE0), which the
   game polls without yielding. The ADXM server threads that complete reads
   never run here, so each poll first runs server passes until they stop
   reading, then the generated status body. */
extern void sub_00190B00_gen(void);
void sub_00190B00(void)
{
    recomp_cri_adxm_server_step();
    sub_00190B00_gen();
}

extern void sub_001907A0_gen(void);
void sub_001907A0(void)
{
    recomp_cri_adxm_server_step();
    sub_001907A0_gen();
}

/* 0x00027720 (USA 0x0002A540) builds a camera object's view and
   projection, the object in EAX; the free camera puts its eye, target and
   field of view in first (free_camera_adapter.c). */
extern void sub_00027720_gen(void);
void sub_00027720(void) { recomp_free_camera_build(sub_00027720_gen); }

/* D3D8 3925 draw, binding and render-state APIs. */
void sub_001DD950(void) { recomp_program_manual_call(0x001dd950u); }

/* DOA3 Sofdec planar color conversion (cdecl). */
void sub_001A3480(void) { recomp_program_manual_call(0x001a3480u); }

/* audio: DirectSoundUseFullHRTF, DSOUND 3936. Host stereo has no HRTF table. */
void sub_001F13C5(void) { recomp_program_manual_call(0x001f13c5u); }
void sub_001F13A5(void) { recomp_program_manual_call(0x001f13a5u); }
void sub_001F13AA(void) { recomp_program_manual_call(0x001f13aau); }
/* DSOUND 4134 IDirectSoundBuffer_SetHeadroom; Japan only. */
void sub_001F1DA5(void) { recomp_program_manual_call(0x001f1da5u); }
/* audio end. */

/* av: CRI ADXM vblank workers. */
void sub_001926F0(void) { recomp_program_manual_call(0x001926f0u); }
void sub_00192760(void) { recomp_program_manual_call(0x00192760u); }
/* av end. */

/* Hand-written game collision passes; geometry helpers remain lifted. */

/* Game: KO afterimage blend setup (0x0006BF00). The original sets the blend
   factors to SRCALPHA and INVSRCALPHA, through D3DDevice_SetRenderState_Simple
   and D3D's state copies, 1,000,000 times. Only the final state is observable
   here, and through the render-state adapter the repeats cost about a second
   per frame. On the Xbox they kept the CPU and GPU busy, which slows the KO,
   so set the state once and wait as long instead. */
void sub_0006BF00(void)
{
    static const uint32_t states[2][3] = {
        {0x00040344u, 0x302u, 0x001eb068u},
        {0x00040348u, 0x303u, 0x001eb06cu},
    };
    const uint32_t entry_esp = recomp_runtime.registers.esp;

    for (unsigned i = 0u; i < 2u; ++i) {
        recomp_runtime.registers.ecx = states[i][0];
        recomp_runtime.registers.edx = states[i][1];
        kernel_call_guest(0x001ddeb0u, NULL, 0u);
        *recomp_memory_u32(states[i][2]) = states[i][1];
    }
    /* shortcut: 30 ms is estimated (about 15 cycles per pass at 733 MHz, and
       16 MB of push buffer for the GPU); calibrate against hardware footage.
       KeDelayExecutionThread(KernelMode, FALSE, relative 100 ns units). */
    recomp_runtime.registers.esp = entry_esp - 8u;
    *recomp_memory_u64(recomp_runtime.registers.esp) = (uint64_t)-300000;
    kernel_call_guest(0x80000063u,
        (const uint32_t[]){0u, 0u, recomp_runtime.registers.esp}, 3u);
    recomp_runtime.registers.esp = entry_esp;
    recomp_runtime.registers.esp += 4u; /* ret */
}

/* Hand-written DOA3 auto-save, bracketed by the save journal (save_adapter.c). */

/* Japan entry point -> the address its adapter is keyed by. Only these reach
   the adapters: other USA keys can be unrelated Japan functions. */
static const uint32_t jp_manual[][2] = {
    {0x0018c934u, 0x00164f50u},
    {0x0018c9c0u, 0x00164fdcu},
    {0x0018c9d3u, 0x00164fefu},
    {0x0018ca12u, 0x0016502eu},
    {0x001926f0u, 0x0016a570u},
    {0x00192760u, 0x0016a5e0u},
    {0x001927d0u, 0x0016a650u},
    {0x001a3480u, 0x001779d0u},
    {0x001dc360u, 0x001b0e00u},
    {0x001dc660u, 0x001b1130u},
    {0x001dd670u, 0x001b1cc0u},
    {0x001dd880u, 0x001b1ea0u},
    {0x001dd950u, 0x001b1f30u},
    {0x001ddeb0u, 0x001b2390u},
    {0x001de080u, 0x001b2470u},
    {0x001de150u, 0x001b2520u},
    {0x001de200u, 0x001b25d0u},
    {0x001de240u, 0x001b2600u},
    {0x001de470u, 0x001b2800u},
    {0x001dec40u, 0x001b3040u},
    {0x001dece0u, 0x001b30b0u},
    {0x001ded70u, 0x001b3130u},
    {0x001def50u, 0x001b32f0u},
    {0x001df010u, 0x001b3390u},
    {0x001df490u, 0x001b3760u},
    {0x001df5f0u, 0x001b38a0u},
    {0x001df690u, 0x001b3940u},
    {0x001dff50u, 0x001b4230u},
    {0x001e0310u, 0x001b45f0u},
    {0x001e14d0u, 0x001e14d0u},
    {0x001e2790u, 0x001b8890u},
    {0x001e27c0u, 0x001b88c0u},
    {0x001e4758u, 0x001ba7d8u},
    {0x001e5283u, 0x001bb1f3u},
    {0x001e52e6u, 0x001bb256u},
    {0x001e5623u, 0x001bb595u},
    {0x001e56f2u, 0x001bb66au},
    {0x001e57edu, 0x001bb770u},
    {0x001e594fu, 0x001bb8a8u},
    {0x001e5b3du, 0x001bba96u},
    {0x001f13a5u, 0x001c6b72u},
    {0x001f13aau, 0x001c6b77u},
    {0x001f13afu, 0x001c6b7cu},
    {0x001f13c5u, 0x001c6b92u},
    {0x001f1d46u, 0x001c7392u},
    {0x001f1d62u, 0x001c73aeu},
    {0x001f1d89u, 0x001c7477u},
    {0x001f1da5u, 0x001f1da5u},
    {0x001f1dc1u, 0x001c7b17u},
    {0x001f1dddu, 0x001c7493u},
    {0x001f1df9u, 0x001c7b33u},
    {0x001f1e1du, 0x001c7b57u},
    {0x001f1e35u, 0x001c7541u},
    {0x001f1e59u, 0x001c7565u},
    {0x001f1e79u, 0x001c7585u},
    {0x001f1e95u, 0x001c75a1u},
    {0x001f1eb5u, 0x001c75c1u},
    {0x001f1ed1u, 0x001c75ddu},
    {0x001f1f01u, 0x001c760du},
    {0x001f23ccu, 0x001c743fu},
    {0x001f23e4u, 0x001c7afbu},
    {0x001f2400u, 0x001c74b3u},
    {0x001f2424u, 0x001c74d7u},
    {0x001f2459u, 0x001c750cu},
    {0x001f29bdu, 0x001c73d5u},
    {0x001f29f2u, 0x001c740au},
    {0x001f2a27u, 0x001c7457u},
    {0x001f2a47u, 0x001c7b6fu},
    {0x001f2cabu, 0x001c7ea9u},
    {0x001f2e65u, 0x001c7fd9u},
    {0x00210b6cu, 0x001e5d4cu},
    {0x00211578u, 0x001e6953u},
    {0x0021157du, 0x001e6958u},
    {0x0021159fu, 0x001e697au},
    {0x00211823u, 0x001e6e3au},
    {0x00211898u, 0x001e6eafu},
    {0x002118a4u, 0x001e6ebbu},
    {0x00211a96u, 0x001e70adu},
    {0x00211b07u, 0x001e711eu},
};

uint32_t recomp_doa3_jp_usa_address(uint32_t guest_address)
{
    size_t low = 0u, high = sizeof jp_manual / sizeof jp_manual[0];

    while (low < high) {
        size_t mid = (low + high) / 2u;
        if (jp_manual[mid][0] == guest_address) return jp_manual[mid][1];
        if (jp_manual[mid][0] < guest_address) low = mid + 1u; else high = mid;
    }
    return 0u;
}
