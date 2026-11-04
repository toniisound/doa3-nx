#include "scene_freeze_adapter.h"
#include "scene_freeze.h"

#include <stdio.h>

#ifdef RECOMP_FULL_PROGRAM
#include "d3d_draw_adapter.h"
#include "d3d_frame_adapter.h"
#include "runtime.h"
#include "doa3_build.h"

/* Scene freeze: stops the game where it is while it goes on drawing, so the
   free camera can look around a still scene. The same design as the DOAXBV
   port's, on DOA3's task table.

   DOA3 runs its systems as cooperative tasks on fibers (docs/doa3-fibers.md).
   The task table at 0x005E5A08 holds 17 records of 32 bytes: +0 the state
   (bit 0 in use, bit 0x10 running), +4 the frames to wait before the next
   turn, +8 its fiber and +0xC its function. The scheduler (0x0009E67B) keeps
   the task it is running in 0x005E5C4C and switches to the task's fiber; the
   task's yield (0x0009E562) clears bit 0x10, stores its wait at +4 and
   switches back to the scheduler's fiber at 0x005E5C48. (Japan 3.1: the
   table at 0x0073C400, the scheduler 0x001C650C, the yield 0x001C65D8.)

   This adapter watches every turn and counts the draws each task issues
   (scene_freeze.c keeps the history). While frozen, a turn of a task that has
   not drawn lately is declined: the switch returns at once and the record is
   left as a yield of one frame leaves it. The drawing tasks keep their turns,
   so the picture stays on screen. The cameras are built by a task that
   draws nothing (0x0002D060); recomp_scene_freeze_keep lets the free camera
   keep it running while frozen, so a frozen scene can still be looked
   around.

   Hiding the overlay is a flag the draw adapter reads: it leaves out the
   game's screen-space draws, frozen or not (d3d_draw_adapter.c), since DOA3
   draws its 2D overlay from the same task as the fighters. Subtitles are
   timed by a clock the freeze does not stop, so a line can end while frozen.

   A step lets every task take its turns until the next frame is presented,
   so a frozen scene can be moved on one frame at a time. */
enum {
    TASK_TABLE = DOA3(0x005E5A08u, 0x0073C400u),
    TASK_RECORD_SIZE = 0x20u,
    TASK_COUNT = 17u,
    TASK_STATE = 0x00u,
    TASK_WAIT = 0x04u,
    TASK_FIBER = 0x08u,
    TASK_FUNCTION = 0x0Cu,
    TASK_STATE_RUNNING = 0x10u,
    TASK_CURRENT = DOA3(0x005E5C4Cu, 0x0073C644u),
    /* Lays out the fighters for the drawing tasks each frame without
       drawing itself; a frozen scene without it loses its fighters. */
    FIGHTER_TASK_FUNCTION = DOA3(0x000B4AC0u, 0x000B4720u),
};

static RecompSceneFreezeModel model;
static bool frozen, overlay_hidden;
static uint32_t turn_draws;
/* While the swap counter is below this, a frozen scene runs as usual. */
static uint32_t step_until;
static RecompSceneFreezeFrameHook frame_hook;
static uint32_t kept_function;
static uint32_t hooked_frame;

void recomp_scene_freeze_set(bool freeze)
{
    if (frozen == freeze) return;
    frozen = freeze;
    fprintf(stderr, "recomp scene freeze: %s", freeze ? "on, drawing tasks" : "off");
    if (freeze) {
        for (uint32_t slot = 0u; slot < TASK_COUNT; ++slot) {
            uint32_t function = *recomp_memory_u32(TASK_TABLE + slot * TASK_RECORD_SIZE + TASK_FUNCTION);
            if (recomp_scene_freeze_keeps(&model, slot, function)) fprintf(stderr, " %08x", function);
        }
    }
    fputc('\n', stderr);
}

bool recomp_scene_freeze_active(void)
{
    return frozen;
}

bool recomp_scene_freeze_holding(void)
{
    return frozen && recomp_d3d_frame_adapter_swap_counter() >= step_until;
}

void recomp_scene_freeze_set_overlay_hidden(bool hidden)
{
    if (overlay_hidden == hidden) return;
    overlay_hidden = hidden;
    fprintf(stderr, "recomp scene freeze: overlay %s\n", hidden ? "hidden" : "shown");
}

bool recomp_scene_freeze_overlay_hidden(void)
{
    return overlay_hidden;
}

void recomp_scene_freeze_set_frame_hook(RecompSceneFreezeFrameHook hook)
{
    frame_hook = hook;
}

void recomp_scene_freeze_keep(uint32_t function)
{
    kept_function = function;
}

void recomp_scene_freeze_step(void)
{
    if (frozen) step_until = recomp_d3d_frame_adapter_swap_counter() + 1u;
}

bool recomp_scene_freeze_switch(uint32_t outgoing_handle, uint32_t target_handle)
{
    uint32_t slot = *recomp_memory_u32(TASK_CURRENT);
    if (slot >= TASK_COUNT) return false;
    uint32_t record = TASK_TABLE + slot * TASK_RECORD_SIZE;
    uint32_t fiber = *recomp_memory_u32(record + TASK_FIBER);
    uint32_t function = *recomp_memory_u32(record + TASK_FUNCTION);
    uint32_t draws = recomp_d3d_draw_adapter_submitted();
    uint32_t frame = recomp_d3d_frame_adapter_swap_counter();
    /* Before holding is read: the hook's controls can step or thaw the scene. */
    if (frozen && frame_hook != NULL && hooked_frame != frame) {
        hooked_frame = frame;
        frame_hook(frame);
    }
    const bool holding = frozen && frame >= step_until;

    if (outgoing_handle == fiber && target_handle != fiber) {
        /* The turn is over. A frozen scene keeps its history as it stood. */
        if (!holding) recomp_scene_freeze_note_turn(&model, slot, function, draws - turn_draws);
        return false;
    }
    if (target_handle != fiber || outgoing_handle == fiber) return false;
    turn_draws = draws;
    if (!holding || recomp_scene_freeze_keeps(&model, slot, function) ||
        function == FIGHTER_TASK_FUNCTION || (kept_function != 0u && function == kept_function))
        return false;
    *recomp_memory_u32(record + TASK_STATE) &= ~TASK_STATE_RUNNING;
    *recomp_memory_u32(record + TASK_WAIT) = 1u;
    return true;
}
#else
void recomp_scene_freeze_set(bool freeze) { (void)freeze; }
bool recomp_scene_freeze_active(void) { return false; }
void recomp_scene_freeze_set_overlay_hidden(bool hidden) { (void)hidden; }
bool recomp_scene_freeze_overlay_hidden(void) { return false; }
void recomp_scene_freeze_set_frame_hook(RecompSceneFreezeFrameHook hook) { (void)hook; }
void recomp_scene_freeze_step(void) { }
void recomp_scene_freeze_keep(uint32_t function) { (void)function; }
bool recomp_scene_freeze_holding(void) { return false; }
bool recomp_scene_freeze_switch(uint32_t outgoing_handle, uint32_t target_handle)
{
    (void)outgoing_handle;
    (void)target_handle;
    return false;
}
#endif
