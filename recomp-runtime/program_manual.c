#include "program_manual.h"
#ifdef RECOMP_FULL_PROGRAM
#include "doa3_collision_adapter.h"
#endif
#include "controller_settings.h"
#include "kernel_abi.h"
#include <string.h>
#include "cri_service_adapter.h"
#include "cri_adxm_adapter.h"
#include "crt_format_adapter.h"
#include "crt_string_adapter.h"
#include "d3d_creation_adapter.h"
#include "d3d_miniport_adapter.h"
#ifdef RECOMP_D3D_FRAME_ENABLED
#include "d3d_draw_adapter.h"
#endif
#ifdef RECOMP_D3D_FRAME_ENABLED
#include "d3d_frame_adapter.h"
#endif
#include "d3d_render_state_adapter.h"
#include "d3d_texture_adapter.h"
#include "d3d_tile_adapter.h"
#include "d3d_vertex_shader_adapter.h"
#include "dsound_service_adapter.h"
#include "dsound_api_adapter.h"
#include "fiber_adapter.h"
#include "input_adapter.h"
#include "save_adapter.h"
#include "soundtrack_adapter.h"
#include "xapi_time_adapter.h"
#include "stop_report.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef RECOMP_FULL_PROGRAM
void sub_0006AFD0(void);
#endif
void recomp_program_thread_start(void);
#ifdef RECOMP_FULL_PROGRAM
void sub_0011F250(void);
#endif

static int shuffle_allows(const uint16_t *masks, int i, int playing, int location)
{
    return i != playing &&
        (location < 0 || location >= 8 || (masks[i] & (1u << location)) != 0u);
}

int recomp_music_shuffle_pick(const uint16_t *masks, int count, int playing,
                              int location, uint32_t random)
{
    int allowed = 0;

    for (int i = 0; i < count; ++i) {
        allowed += shuffle_allows(masks, i, playing, location);
    }
    if (allowed == 0) {
        return -1;
    }
    random %= (uint32_t)allowed;
    for (int i = 0; i < count; ++i) {
        if (shuffle_allows(masks, i, playing, location) && random-- == 0u) {
            return i;
        }
    }
    return -1;
}

#ifdef RECOMP_FULL_PROGRAM
static void recomp_start_consumer_adapter(void)
{
    static int reported;
    uint32_t packet = *recomp_memory_u32(0x005f2e99u);
    uint32_t current = *recomp_memory_u32(0x005f3138u);
    uint32_t rising = *recomp_memory_u32(0x005f3140u);

    if (!reported && (rising & 0x10u) != 0u) {
        reported = 1;
        fprintf(
            stderr,
            "recomp input: START consumer packet=0x%08" PRIx32
            " current=0x%08" PRIx32 " rising=0x%08" PRIx32 "\n",
            packet,
            current,
            rising);
        recomp_stop_at_boundary("input-start-consumer:first");
    }
    sub_0006AFD0();
}
#endif

#ifdef RECOMP_FULL_PROGRAM
/* Round 30. sub_0011F250 is the per-view frame entry: it takes a view index
   and uses it to select both the camera block (0x009EEE70 + index * 0x1B0)
   and the per-view texture tables (0xB2D680 + index * 0xA00). Round 21
   showed two smooth camera streams alternating between frames and asked what
   supplies that index; rounds 22-26 then sampled globals believed to feed it
   and found all of them constant.

   The argument itself was never read. At entry it is the dword above the
   return slot, so observe it here and chain to the generated body, the same
   shape as recomp_start_consumer_adapter. The chain call is unconditional,
   so behaviour is identical whether or not the probe is enabled. */
static void recomp_view_entry_adapter(void)
{
    static const char *view_arg_trace;
    static bool view_arg_trace_read;
    static uint32_t view_arg_lines;

    if (!view_arg_trace_read) {
        view_arg_trace_read = true;
        view_arg_trace = getenv("RECOMP_D3D_VIEWARG");
    }
    if (view_arg_trace != NULL && view_arg_lines < 240u) {
        const uint32_t *argument =
            recomp_memory_u32(recomp_runtime.registers.esp + 4u);

        if (argument != NULL) {
            ++view_arg_lines;
            fprintf(
                stderr,
                "recomp d3d viewarg: swap=%" PRIu32 " index=%" PRIu32
                "\n",
                recomp_d3d_frame_adapter_swap_counter(),
                *argument);
        }
    }
    sub_0011F250();
}
#endif

#ifdef RECOMP_FULL_PROGRAM
void sub_0001B340(void);
void sub_000E8100(void);
void sub_000B8F70(void);
void sub_000B9790(void);

static void restore_controller_settings(void)
{
    uint8_t modes[4];
    sub_0001B340();
    if (!recomp_controller_settings_load(recomp_disc_root_path, modes)) {
        recomp_stop(1, "controls:read-preference");
        return;
    }
    memcpy(recomp_memory(0x0041655du, sizeof modes), modes, sizeof modes);
}

static void update_controller_settings(void)
{
    uint8_t before[4];
    uint8_t *modes = recomp_memory(0x0041655du, sizeof before);
    memcpy(before, modes, sizeof before);
    sub_000E8100();
    if (memcmp(before, modes, sizeof before) != 0 &&
        !recomp_controller_settings_save(recomp_disc_root_path, modes)) {
        recomp_stop(1, "controls:write-preference");
    }
}

/* Radio shuffle. The game's music player starts the playlist song at
   0x000B8F70 and runs each frame at 0x000B9790 (EAX is the player). Both
   leave the playlist's next index one past the playing song, wrapping at the
   end; the game has no random mode. With RECOMP_MUSIC_SHUFFLE=1 that step is
   replaced by a random entry allowed at the current location. The host
   random source leaves the game's own rand sequence untouched. */
enum {
    PLAYER_PLAYLIST = 0x658u,
    PLAYER_LOCATION = 0x6e0u,
    PLAYLIST_ENTRIES = 0x644u,
    PLAYLIST_NEXT = 0x64cu,
    PLAYLIST_PLAYING = 0x650u,
    PLAYLIST_CAPACITY = 100,
    ENTRY_SIZE = 0x10u,
    ENTRY_LOCATION_MASK = 6u
};

static int music_shuffle_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *setting = getenv("RECOMP_MUSIC_SHUFFLE");

        enabled = setting != NULL && strcmp(setting, "1") == 0;
    }
    return enabled;
}

/* Private xorshift32 state, so no shared C runtime rand state is touched. */
static uint32_t music_shuffle_random(void)
{
    static uint32_t state;

    if (state == 0u) {
        state = (uint32_t)time(NULL) | 1u;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void music_shuffle_after(uint32_t player, uint32_t list_before, uint32_t next_before)
{
    uint32_t list = *recomp_memory_u32(player + PLAYER_PLAYLIST);
    uint16_t masks[PLAYLIST_CAPACITY];
    int count, next, playing, pick;

    if (list == 0u) {
        return;
    }
    count = (int)*recomp_memory_u32(list);
    next = (int)*recomp_memory_u32(list + PLAYLIST_NEXT);
    playing = (int)*recomp_memory_u32(list + PLAYLIST_PLAYING);
    if (count < 2 || count > PLAYLIST_CAPACITY ||
            (list == list_before && (uint32_t)next == next_before) ||
            next != (playing + 1) % count) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        uint32_t entry = *recomp_memory_u32(list + PLAYLIST_ENTRIES) + (uint32_t)i * ENTRY_SIZE;

        memcpy(&masks[i], recomp_memory(entry + ENTRY_LOCATION_MASK, 2u), 2u);
    }
    pick = recomp_music_shuffle_pick(masks, count, playing,
        (int8_t)*recomp_memory(player + PLAYER_LOCATION, 1u), music_shuffle_random());
    if (pick >= 0) {
        *recomp_memory_u32(list + PLAYLIST_NEXT) = (uint32_t)pick;
        fprintf(stderr, "[music-shuffle] next=%d of %d\n", pick, count);
    }
}

static void music_shuffle_call(void (*original)(void))
{
    uint32_t player = recomp_runtime.registers.eax;
    uint32_t list = 0u, next = 0u;

    if (music_shuffle_enabled()) {
        list = *recomp_memory_u32(player + PLAYER_PLAYLIST);
        next = list != 0u ? *recomp_memory_u32(list + PLAYLIST_NEXT) : 0u;
    }
    original();
    if (music_shuffle_enabled()) {
        music_shuffle_after(player, list, next);
    }
}

static void music_start_song(void)
{
    music_shuffle_call(sub_000B8F70);
}

static void music_update(void)
{
    music_shuffle_call(sub_000B9790);
}

/* 0x000C9CC0: init of task 0x13, the hotel room menu's View Collection
   (#64). Only a data store at 0x000DFDED references it, so the lifter never
   found it. It points three UI slots at the task's handlers and hands each
   the caller's context. */
static void enter_collection_screen(void)
{
    static const struct {
        uint32_t slot_offset;
        uint32_t handler;
    } slots[] = {
        {0x65cu, 0x000ca0f0u},
        {0x640u, 0x000c9d10u},
        {0x64cu, 0x000c9d20u},
    };
    uint32_t context = kernel_arg(2);
    uint32_t ui = *recomp_memory_u32(*recomp_memory_u32(0x00317764u) + 0xcu);

    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        uint32_t slot = *recomp_memory_u32(ui + slots[i].slot_offset);

        *recomp_memory_u32(slot + 4u) = slots[i].handler;
        *recomp_memory_u32(slot + 8u) = context;
    }
    /* Leave the scratch registers as the original does. */
    recomp_runtime.registers.ecx = *recomp_memory_u32(ui + 0x640u);
    recomp_runtime.registers.edx = context;
    kernel_return_caller_cleanup(1u);
}
#endif

/* Every binding below names a DOAXBV (XDK 4928) guest address. In DOA3 those
   addresses are unrelated code, so the bindings are off unless
   RECOMP_DOAXBV_BINDINGS is defined; only the adapter tests define it.
   Re-enable an adapter for DOA3 by re-binding it to its DOA3 address and
   moving it into the DOA3 group. */
RecompFunction recomp_lookup_manual(uint32_t guest_address)
{
#ifdef RECOMP_DOA3_JP_BINDINGS
    guest_address = recomp_doa3_jp_usa_address(guest_address);
    if (guest_address == 0u) return NULL;
#endif
#ifndef RECOMP_DOAXBV_BINDINGS
#ifdef RECOMP_FULL_PROGRAM
    /* Temporary game seam: only the boundary exemption pair is handwritten.
       Wall/transfer passes and geometry stay lifted pending full equivalence. */
    RecompFunction collision = recomp_doa3_collision_lookup(guest_address);
    if (collision != NULL) return collision;
    /* DOA3 auto-save, hand-written in save_adapter.c around the save journal. */
    if (guest_address == 0x00021930u) return recomp_save_lookup_manual(guest_address);
#endif
    /* DOA3: XPP input entry points (XAPILIB 3911) on the native input model. */
    RecompFunction function = recomp_input_lookup_manual(guest_address);
    /* FIBER: DOA3 XAPILIB 3911 stack transfer (docs/doa3-fibers.md).
       Replace the library boundary: a generated C return cannot resume the
       guest continuation on another stack. No game task is replaced. */
    if (function == NULL) {
        function = recomp_fiber_lookup_manual(
            guest_address, &recomp_fiber_doa3_bindings);
    }
    /* FIBER end. */
    /* Temporary CRI boundary: the main worker is absent from the lift and is
       referenced only as a thread callback. Reuse the ADXM server model;
       no game callback is replaced. The recipe also marks this manual. */
    if (guest_address == 0x0016a650u) return recomp_cri_adxm_main_thread;
    /* av: native ADXM vblank workers account for elapsed refreshes. Temporary
       seam: the registered middleware servers and decoder remain generated. */
    if (guest_address == 0x0016a570u) return recomp_cri_adxm_vblank_a_thread;
    if (guest_address == 0x0016a5e0u) return recomp_cri_adxm_vblank_b_thread;
    /* av end. */
    /* DOA3 Sofdec color conversion; decoder and frame lifecycle stay generated. */
    if (guest_address == 0x001779d0u)
        return recomp_cri_service_lookup_manual(guest_address);
    /* DOA3: the D3D 3925 GPU layer under CDevice::Init, and KickOff. */
    if (function == NULL) {
        function = recomp_d3d_miniport_lookup_manual(guest_address);
    }
    /* DOA3 D3D8 3925 draw/state APIs; layouts and temporary seams are
       recorded in docs/doa3-d3d-draw.md. DOAXBV lookups remain gated. */
    if (function == NULL) function = recomp_d3d_texture_lookup_manual(guest_address);
    if (function == NULL) function = recomp_d3d_vertex_shader_lookup_manual(guest_address);
    if (function == NULL) function = recomp_d3d_render_state_lookup_manual(guest_address);
    if (function == NULL) function = recomp_d3d_tile_lookup_manual(guest_address);
#ifdef RECOMP_D3D_FRAME_ENABLED
    if (function == NULL) function = recomp_d3d_draw_lookup_manual(guest_address);
#endif
    /* DOA3: the DirectSound 3936 wrappers game code calls. */
    if (function == NULL) {
        function = recomp_dsound_api_lookup_manual(guest_address);
    }
#ifdef RECOMP_D3D_FRAME_ENABLED
    /* DOA3: D3DDevice_SetGammaRamp, Clear, Present (docs/doa3-d3d-frame.md). */
    if (function == NULL) {
        function = recomp_d3d_frame_lookup_manual(guest_address);
    }
#endif
    return function;
#else
    RecompFunction function = recomp_cri_service_lookup_manual(guest_address);

    if (function == NULL) {
        function = recomp_d3d_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_d3d_render_state_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_d3d_texture_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_d3d_tile_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_d3d_vertex_shader_lookup_manual(guest_address);
    }
#ifdef RECOMP_D3D_FRAME_ENABLED
    if (function == NULL) {
        function = recomp_d3d_frame_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_d3d_draw_lookup_manual(guest_address);
    }
#endif
    if (function == NULL) {
        function = recomp_dsound_service_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_input_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_crt_format_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_crt_string_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_fiber_lookup_manual(
            guest_address, &recomp_fiber_doaxbv_bindings);
    }
    if (function == NULL) {
        function = recomp_save_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_soundtrack_lookup_manual(guest_address);
    }
    if (function == NULL) {
        function = recomp_xapi_time_lookup_manual(guest_address);
    }
#ifdef RECOMP_FULL_PROGRAM
    if (function == NULL && guest_address == 0x0001b340u) {
        function = restore_controller_settings;
    }
    if (function == NULL && guest_address == 0x000e8100u) {
        function = update_controller_settings;
    }
    if (function == NULL && guest_address == 0x0006afd0u) {
        function = recomp_start_consumer_adapter;
    }
    if (function == NULL && guest_address == 0x0011f250u) {
        function = recomp_view_entry_adapter;
    }
    if (function == NULL && guest_address == 0x000c9cc0u) {
        function = enter_collection_screen;
    }
    if (function == NULL && guest_address == 0x000b8f70u) {
        function = music_start_song;
    }
    if (function == NULL && guest_address == 0x000b9790u) {
        function = music_update;
    }
#endif
    if (function == NULL && guest_address == 0x0018322du) {
        function = recomp_program_thread_start;
    }
    return function;
#endif
}


/* DOA3's boot routine 0x00021F70 holds 0x004B8438 at 1 from entry through
   the legal notice (loop 0x00022330: 30 x the frame-rate byte at 0x002FD55C
   frames, no button test) and the save load after it. Run those frames
   without host waits. Saves later reuse the flag, so this ends for good the
   first time the flag clears. */
bool recomp_doa3_boot_fast_forward(void)
{
    static bool seen, done;
    const bool busy = !done && *recomp_memory(0x004b8438u, 1u) == 1u;

    if (busy && !seen) {
        seen = true;
        fprintf(stderr, "recomp boot: legal notice fast-forward on\n");
    } else if (!busy && seen && !done) {
        done = true;
        fprintf(stderr, "recomp boot: legal notice fast-forward off\n");
    }
    return busy;
}
