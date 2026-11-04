/* Library entry points rebound to hand-written adapters.

   The lifter generates no body for an address listed in
   tools/doa3/manual_functions.json and emits every call to it as indirect
   dispatch, which tries recomp_lookup_manual() first. The dispatch table and
   recomp_funcs.h still name sub_X, so define it here as one
   recomp_program_manual_call() call, e.g.
     void sub_001B3390(void) { recomp_program_manual_call(0x001b3390u); }
   It runs only when no adapter is bound, and then stops naming the address. */
#include "program_manual.h"
#include "stop_report.h"
#include "cri_adxm_adapter.h"
#include "free_camera_adapter.h"

#include <inttypes.h>

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
void sub_00164F50(void) { recomp_program_manual_call(0x00164f50u); }
void sub_00164FDC(void) { recomp_program_manual_call(0x00164fdcu); }
void sub_00164FEF(void) { recomp_program_manual_call(0x00164fefu); }
void sub_0016502E(void) { recomp_program_manual_call(0x0016502eu); }
/* FIBER end. */

/* D3DDevice_SetGammaRamp, D3DDevice_Clear, D3DDevice_Present (XDK 3925).
   Their adapters are in d3d_frame_adapter.c; see docs/doa3-d3d-symbols.md. */
void sub_001B0E00(void) { recomp_program_manual_call(0x001b0e00u); }
void sub_001B3390(void) { recomp_program_manual_call(0x001b3390u); }
void sub_001B1130(void) { recomp_program_manual_call(0x001b1130u); }
void sub_001B5850(void) { recomp_program_manual_call(0x001b5850u); }

/* XInitDevices and its jmp thunk, XGetDevices, XGetDeviceChanges, XInputOpen,
   XInputClose, XInputGetCapabilities, XInputGetState, XInputSetState
   (XAPILIB 3911, XPP section). Adapters are in input_adapter.c. */
void sub_001E5D4C(void) { recomp_program_manual_call(0x001e5d4cu); }
void sub_001E6953(void) { recomp_program_manual_call(0x001e6953u); }
void sub_001E6958(void) { recomp_program_manual_call(0x001e6958u); }
void sub_001E697A(void) { recomp_program_manual_call(0x001e697au); }
void sub_001E6E3A(void) { recomp_program_manual_call(0x001e6e3au); }
void sub_001E6EAF(void) { recomp_program_manual_call(0x001e6eafu); }
void sub_001E6EBB(void) { recomp_program_manual_call(0x001e6ebbu); }
void sub_001E70AD(void) { recomp_program_manual_call(0x001e70adu); }
void sub_001E711E(void) { recomp_program_manual_call(0x001e711eu); }

/* CDevice::KickOff and the miniport GPU layer under CDevice::Init (D3D8 3925).
   Adapters are in d3d_miniport_adapter.c. */
void sub_001B88C0(void) { recomp_program_manual_call(0x001b88c0u); }
void sub_001BB770(void) { recomp_program_manual_call(0x001bb770u); }
void sub_001BB1F3(void) { recomp_program_manual_call(0x001bb1f3u); }
void sub_001BB256(void) { recomp_program_manual_call(0x001bb256u); }
void sub_001BB595(void) { recomp_program_manual_call(0x001bb595u); }
void sub_001BB66A(void) { recomp_program_manual_call(0x001bb66au); }
void sub_001BA7D8(void) { recomp_program_manual_call(0x001ba7d8u); }
void sub_001BB8A8(void) { recomp_program_manual_call(0x001bb8a8u); }
void sub_001BBA96(void) { recomp_program_manual_call(0x001bba96u); }
void sub_001B8890(void) { recomp_program_manual_call(0x001b8890u); }

/* DirectSound 3936 wrappers game code calls (dsound_api_adapter.c). */
void sub_001C7FD9(void) { recomp_program_manual_call(0x001c7fd9u); }
void sub_001C760D(void) { recomp_program_manual_call(0x001c760du); }
void sub_001C7392(void) { recomp_program_manual_call(0x001c7392u); }
void sub_001C73AE(void) { recomp_program_manual_call(0x001c73aeu); }
void sub_001C73D5(void) { recomp_program_manual_call(0x001c73d5u); }
void sub_001C740A(void) { recomp_program_manual_call(0x001c740au); }
void sub_001C743F(void) { recomp_program_manual_call(0x001c743fu); }
void sub_001C7457(void) { recomp_program_manual_call(0x001c7457u); }
void sub_001C7EA9(void) { recomp_program_manual_call(0x001c7ea9u); }
void sub_001C6B7C(void) { recomp_program_manual_call(0x001c6b7cu); }
void sub_001C7477(void) { recomp_program_manual_call(0x001c7477u); }
void sub_001C7493(void) { recomp_program_manual_call(0x001c7493u); }
void sub_001C74B3(void) { recomp_program_manual_call(0x001c74b3u); }
void sub_001C74D7(void) { recomp_program_manual_call(0x001c74d7u); }
void sub_001C750C(void) { recomp_program_manual_call(0x001c750cu); }
void sub_001C7541(void) { recomp_program_manual_call(0x001c7541u); }
void sub_001C7565(void) { recomp_program_manual_call(0x001c7565u); }
void sub_001C7585(void) { recomp_program_manual_call(0x001c7585u); }
void sub_001C75A1(void) { recomp_program_manual_call(0x001c75a1u); }
void sub_001C75C1(void) { recomp_program_manual_call(0x001c75c1u); }
void sub_001C75DD(void) { recomp_program_manual_call(0x001c75ddu); }
void sub_001C7AFB(void) { recomp_program_manual_call(0x001c7afbu); }
void sub_001C7B17(void) { recomp_program_manual_call(0x001c7b17u); }
void sub_001C7B33(void) { recomp_program_manual_call(0x001c7b33u); }
void sub_001C7B57(void) { recomp_program_manual_call(0x001c7b57u); }
void sub_001C7B6F(void) { recomp_program_manual_call(0x001c7b6fu); }

void sub_0016A650(void) { recomp_program_manual_call(0x0016a650u); }

/* CRI ADXF_GetPtStat (0x00169150) and ADXF_GetStat (0x00168DE0), which the
   game polls without yielding. The ADXM server threads that complete reads
   never run here, so each poll first runs server passes until they stop
   reading, then the generated status body. */
extern void sub_00169150_gen(void);
void sub_00169150(void)
{
    recomp_cri_adxm_server_step();
    sub_00169150_gen();
}

extern void sub_00168DE0_gen(void);
void sub_00168DE0(void)
{
    recomp_cri_adxm_server_step();
    sub_00168DE0_gen();
}

/* D3D8 3925 draw, binding and render-state APIs. */
void sub_001B3760(void) { recomp_program_manual_call(0x001b3760u); }
void sub_001B38A0(void) { recomp_program_manual_call(0x001b38a0u); }
void sub_001B3940(void) { recomp_program_manual_call(0x001b3940u); }
void sub_001B1CC0(void) { recomp_program_manual_call(0x001b1cc0u); }
void sub_001B45F0(void) { recomp_program_manual_call(0x001b45f0u); }
void sub_001B4230(void) { recomp_program_manual_call(0x001b4230u); }
void sub_001B1EA0(void) { recomp_program_manual_call(0x001b1ea0u); }
void sub_001B2390(void) { recomp_program_manual_call(0x001b2390u); }
void sub_001B2470(void) { recomp_program_manual_call(0x001b2470u); }
void sub_001B2520(void) { recomp_program_manual_call(0x001b2520u); }
void sub_001B25D0(void) { recomp_program_manual_call(0x001b25d0u); }
void sub_001B2600(void) { recomp_program_manual_call(0x001b2600u); }
void sub_001B2800(void) { recomp_program_manual_call(0x001b2800u); }
void sub_001B3040(void) { recomp_program_manual_call(0x001b3040u); }
void sub_001B30B0(void) { recomp_program_manual_call(0x001b30b0u); }
void sub_001B3130(void) { recomp_program_manual_call(0x001b3130u); }
void sub_001B32F0(void) { recomp_program_manual_call(0x001b32f0u); }
void sub_001B1F30(void) { recomp_program_manual_call(0x001b1f30u); }

/* DOA3 Sofdec planar color conversion (cdecl). */
void sub_001779D0(void) { recomp_program_manual_call(0x001779d0u); }

/* audio: DirectSoundUseFullHRTF, DSOUND 3936. Host stereo has no HRTF table. */
void sub_001C6B92(void) { recomp_program_manual_call(0x001c6b92u); }
void sub_001C6B72(void) { recomp_program_manual_call(0x001c6b72u); }
void sub_001C6B77(void) { recomp_program_manual_call(0x001c6b77u); }
/* audio end. */

/* av: CRI ADXM vblank workers. */
void sub_0016A570(void) { recomp_program_manual_call(0x0016a570u); }
void sub_0016A5E0(void) { recomp_program_manual_call(0x0016a5e0u); }
/* av end. */

/* Hand-written game collision passes; geometry helpers remain lifted. */
void sub_000A1380(void) { recomp_program_manual_call(0x000a1380u); }
void sub_000A9AC0(void) { recomp_program_manual_call(0x000a9ac0u); }
void sub_0008D180(void) { recomp_program_manual_call(0x0008d180u); }
void sub_000A40E0(void) { recomp_program_manual_call(0x000a40e0u); }

/* Hand-written DOA3 auto-save, bracketed by the save journal (save_adapter.c). */
void sub_00021930(void) { recomp_program_manual_call(0x00021930u); }

/* Game code wrapped, not replaced: the lifter emits the body as sub_X_gen
   (it reads the extern below) and sends every call to sub_X here.
   0x0002A540 builds a camera object's view and projection; the free camera
   puts its eye, target and field of view in first (free_camera_adapter.c). */
extern void sub_0002A540_gen(void);
void sub_0002A540(void) { recomp_free_camera_build(sub_0002A540_gen); }
