#include "free_camera_adapter.h"
#include "costume_inject.h"
#include "doa3_build.h"
#include "screenshot.h"
#include "fiber_adapter.h"
#include "free_camera.h"
#include "input_host_win32.h"
#include "input_pulse_source.h"
#include "overlay_text.h"
#include "scene_freeze_adapter.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef RECOMP_FULL_PROGRAM
#include "d3d_frame_adapter.h"

/* Free camera: RECOMP_FREE_CAMERA=1 lets F1, ` (backquote) or clicking the
   right stick take the game's camera and fly it. The flying itself is free_camera.c, shared with the
   DOAXBV port; this file is DOA3's side of it, with the DOAXBV port's
   controls.

   DOA3 keeps five camera objects of 0xCC0 bytes from 0x003C29C0 (Japan
   3.1: 0xC80 bytes from 0x004DAE90). The game's camera logic sets the eye
   at +0x00, the target at +0x10, the vertical field of view at +0x30 and
   the roll at +0x34 (both in radians), and keeps a working copy of them at
   +0xA8 (eye), +0xB8 (target), +0xD8 (field of view) and +0xDC (roll).
   Each frame 0x0002A540 (Japan 0x00027720) builds one from the working
   copy of the object in ESI (Japan: EAX): it writes the view and
   projection matrices at +0x150 and +0x190 that the frame's draws use,
   rolled by +0xDC. program_forwards.c (program_forwards_jp.c) wraps that build (the lifter keeps its body
   as sub_0002A540_gen); the wrapper puts the free camera's eye, target and
   field of view in both places for that build and leaves them there, so the
   game also culls for the free camera (as in DOAXBV, where putting its own
   back made parts at the screen's edges vanish). It levels the working
   copy's roll too: the game keeps animating +0x34 (intro shots roll up to
   about 50 degrees, fights sway it slightly), and the free camera has no
   roll of its own. When the camera is put away
   the game's camera logic takes the object over again.

   The free camera flies the first object built after it is taken up (in a
   fight, the first: 0x003C29C0), again whenever the one it flies stops being
   built, and C or the X button moves it to the next object the game built in
   the last frame.

   While it flies, the pad and keyboard are its own and the game sees a still
   pad: left stick or WASD move, right stick, the arrow keys or a right-mouse
   drag look, the triggers or Q / E go down / up, A or Shift is fast, B or
   Ctrl slow, Z / X zoom (keys only, as in DOAXBV). Y or R returns to the
   view it started from, and the d-pad's left or \ turns it to the next
   fighter and chooses her. The d-pad's up and down, or = and -, move the
   chosen fighter's swimsuit (hold to scroll; it is worn when let go). The
   left stick's click opens the swimsuit menu as it does without the camera
   (costume_inject.c); while it is open the menu has the pad. The
   arrow keys press the d-pad for the game, so the camera leaves those
   buttons out while the keys are down. F12, or the d-pad's right while flying,
   saves a screenshot of the game's frame (screenshot.c). Back is never a
   control: DOA3 acts on it (it resets training and skips cutscenes) before
   a combo could catch it.

   P, or START while flying, freezes the scene (scene_freeze_adapter.c);
   H or the left shoulder hides the game's 2D overlay and this panel, and .
   or the right shoulder moves a frozen scene on one frame (held, it keeps
   stepping). Putting the camera away thaws the scene. The game polls the
   controller from a task the freeze skips, so while frozen the freeze calls
   back once a frame and the controls are sampled here. */
enum {
    CAMERA_FIRST = DOA3(0x003C29C0u, 0x004DAE90u),
    CAMERA_STRIDE = DOA3(0x0CC0u, 0x0C80u),
    CAMERA_COUNT = 5u,
    CAMERA_EYE = 0x00u,
    CAMERA_TARGET = 0x10u,
    CAMERA_FOV = 0x30u,
    BUILD_EYE = 0xA8u,
    BUILD_TARGET = 0xB8u,
    BUILD_FOV = 0xD8u,
    BUILD_ROLL = 0xDCu,
    XBOX_DPAD_UP = 0x0001u,
    XBOX_DPAD_DOWN = 0x0002u,
    XBOX_DPAD_LEFT = 0x0004u,
    XBOX_DPAD_RIGHT = 0x0008u,
    XBOX_START = 0x0010u,
    XBOX_BACK = 0x0020u,
    XBOX_RIGHT_THUMB = 0x0080u,
    ANALOG_PRESSED = 0x20u,
    STEP_REPEAT_AFTER_MS = 500u,
    FIGHTERS = 4u,
    /* The task that updates and builds the cameras; it draws nothing. */
    CAMERA_TASK_FUNCTION = DOA3(0x0002D060u, 0x000235B0u),
};

/* Each fighter's bones (the costume injector's bank_base), hips first. */
static const uint32_t fighter_bones[FIGHTERS] = {
    DOA3(0x004785d0u, 0x00590850u), DOA3(0x00478b90u, 0x00590e10u),
    DOA3(0x00479150u, 0x005913d0u), DOA3(0x00479710u, 0x00591990u),
};
static const float STICK_DEADZONE = 0.15f;
/* At a field of view of one radian. */
static const float MOUSE_RADIANS_PER_PIXEL = 0.004f;
/* DOA3's fights are tight, so the camera flies at twice the shared model's
   speed (free_camera.c: half the eye-to-target distance per second). */
static const float SPEED_SCALE = 2.0f;
/* The camera steps once per build of the object it flies, once a frame, by
   the time since its last step; after a stall longer than MAX_STEP_SECONDS
   it steps one 60 Hz frame instead, so it does not jump. */
static const float FRAME_SECONDS = 1.0f / 60.0f;
static const double MAX_STEP_SECONDS = 0.1;

static struct {
    int enabled;              /* -1 until RECOMP_FREE_CAMERA is read */
    bool active, started;
    bool toggle_held, freeze_held, overlay_held, cycle_held, reset_held, aim_held, step_held, shot_held;
    bool r3_held;             /* the right stick's click, last sample */
    /* The fighter whose swimsuit the d-pad moves (0-3; her team is & 1). */
    unsigned chosen;
    struct { int dir; uint64_t since, last; } suit_hold;
    uint64_t step_since;
    uint32_t object;          /* the camera object flown, 0 for the next built */
    double stepped_seconds;   /* when the camera last stepped */
    RecompFreeCamera camera, home;
    RecompFreeCameraControls controls;
    /* Objects built in this frame and the last (bit per object). */
    uint32_t built_frame, built_now, built_last;
    bool cycle_pending, reset_pending;
    int aim_fighter;          /* next fighter to turn to; -1 none pending */
    unsigned aim_next;
    /* The hint lists pad buttons once a pad was last used, keys once a key was. */
    int controller_hints;
    /* Test hooks: RECOMP_FREE_CAMERA_AT=<build> takes the camera up at that
       build of any object; RECOMP_FREE_CAMERA_DRIFT=right,up,forward,turn,pitch
       holds those controls (each -1 to 1) from then on;
       RECOMP_FREE_CAMERA_FREEZE_AT=<frame> freezes the scene at that
       presented frame. */
    unsigned long take_at, freeze_at, builds;
    bool drifting;
    RecompFreeCameraControls drift;
} state = {.enabled = -1, .aim_fighter = -1, .controller_hints = -1};

static bool enabled(void)
{
    if (state.enabled < 0) {
        const char *text = getenv("RECOMP_FREE_CAMERA");
        state.enabled = text != NULL && *text == '1';
        const char *at = getenv("RECOMP_FREE_CAMERA_AT");
        state.take_at = at ? strtoul(at, NULL, 10) : 0;
        const char *freeze = getenv("RECOMP_FREE_CAMERA_FREEZE_AT");
        state.freeze_at = freeze ? strtoul(freeze, NULL, 10) : 0;
        const char *drift = getenv("RECOMP_FREE_CAMERA_DRIFT");
        if (drift) {
            float v[5] = {0};
            sscanf(drift, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]);
            state.drift.move[0] = v[0]; state.drift.move[1] = v[1]; state.drift.move[2] = v[2];
            state.drift.look[0] = v[3]; state.drift.look[1] = v[4];
            state.drifting = true;
        }
    }
    return state.enabled != 0;
}

bool recomp_free_camera_flying(void)
{
    return state.active;
}

static void capture(uint32_t port, RecompInputGamepad *g, uint16_t stick_clicks);
static void frozen_frame(uint32_t frame);
static void hint_panel(void);

bool recomp_free_camera_install(void)
{
    if (!enabled()) return false;
    recomp_input_host_set_capture_hook(capture);
    recomp_fiber_set_switch_hook(recomp_scene_freeze_switch);
    recomp_scene_freeze_set_frame_hook(frozen_frame);
    return true;
}

static float stick(int16_t value)
{
    float v = value / 32767.0f;
    if (fabsf(v) < STICK_DEADZONE) return 0.0f;
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

static bool pressed(const RecompInputGamepad *g, unsigned analog)
{
    return g->analog_buttons[analog] >= ANALOG_PRESSED;
}

static double seconds_now(void)
{
    struct timespec now;
    return timespec_get(&now, TIME_UTC) == 0 ? 0.0 : (double)now.tv_sec + now.tv_nsec / 1e9;
}

static void set_active(bool active)
{
    state.active = active;
    state.started = false;
    state.object = 0;
    state.aim_fighter = -1;
    /* A frozen scene keeps building the camera while it flies. */
    recomp_scene_freeze_keep(active ? CAMERA_TASK_FUNCTION : 0u);
    if (!active) {
        recomp_scene_freeze_set(false);
        recomp_scene_freeze_set_overlay_hidden(false);
        RecompOverlayText empty;
        recomp_overlay_clear(&empty);
        recomp_overlay_publish(&empty);
    } else {
        /* Take the overlay from a swimsuit panel still on screen. */
        hint_panel();
    }
    fprintf(stderr, "recomp free camera: %s\n", active ? "on" : "off");
}

static void hint_panel(void)
{
    /* The swimsuit menu has the overlay while it is open. */
    if (recomp_costume_inject_menu_open()) return;
    RecompOverlayText text;
    recomp_overlay_clear(&text);
    if (state.active && !recomp_scene_freeze_overlay_hidden()) {
        recomp_overlay_set_title(&text, "Free camera");
        if (state.started)
            recomp_overlay_add_line(&text, "Camera %u%s", (state.object - CAMERA_FIRST) / CAMERA_STRIDE + 1u,
                recomp_scene_freeze_active() ? "   Frozen" : "   Running");
        char suit[RECOMP_OVERLAY_LINE_SIZE];
        if (recomp_costume_inject_describe(state.chosen & 1u, suit, sizeof suit))
            recomp_overlay_add_line(&text, "%s", suit);
        recomp_overlay_set_hint(&text, state.controller_hints > 0
            ? "Sticks fly  LT/RT down/up  A fast  B slow  L3 suits\n"
              "X camera  Y reset  D-pad left fighter  D-pad up/down suit\n"
              "Start freeze  RB step  LB hide  D-pad right screenshot  R3 off"
            : "WASD fly  Q/E down/up  arrows or right-drag look  Z/X zoom\n"
              "Shift fast  Ctrl slow  C camera  R reset  \\ fighter  -/= suit\n"
              "P freeze  . step  H hide  F12 screenshot  F1 off");
    }
    recomp_overlay_publish(&text);
}

/* The input host presses pad buttons for some keys (A S Z X Q W E R, the
   arrows, Return, Backspace). The camera reads those keys itself, so it
   leaves their buttons out while they are down: otherwise S would press Y
   and reset the view, and A would press X and change camera. */
static void leave_out_keys(RecompInputGamepad *g)
{
    static const struct { RecompInputHostKey key; unsigned analog; } analog_keys[] = {
        {RECOMP_INPUT_HOST_KEY_A, RECOMP_INPUT_ANALOG_X}, {RECOMP_INPUT_HOST_KEY_S, RECOMP_INPUT_ANALOG_Y},
        {RECOMP_INPUT_HOST_KEY_Z, RECOMP_INPUT_ANALOG_A}, {RECOMP_INPUT_HOST_KEY_X, RECOMP_INPUT_ANALOG_B},
        {RECOMP_INPUT_HOST_KEY_Q, RECOMP_INPUT_ANALOG_WHITE}, {RECOMP_INPUT_HOST_KEY_W, RECOMP_INPUT_ANALOG_BLACK},
        {RECOMP_INPUT_HOST_KEY_E, RECOMP_INPUT_ANALOG_LTRIG}, {RECOMP_INPUT_HOST_KEY_R, RECOMP_INPUT_ANALOG_RTRIG},
    };
    static const struct { RecompInputHostKey key; uint16_t button; } button_keys[] = {
        {RECOMP_INPUT_HOST_KEY_UP, 0x0001u}, {RECOMP_INPUT_HOST_KEY_DOWN, 0x0002u},
        {RECOMP_INPUT_HOST_KEY_LEFT, 0x0004u}, {RECOMP_INPUT_HOST_KEY_RIGHT, 0x0008u},
        {RECOMP_INPUT_HOST_KEY_RETURN, XBOX_START}, {RECOMP_INPUT_HOST_KEY_BACKSPACE, XBOX_BACK},
    };
    for (size_t i = 0; i < sizeof analog_keys / sizeof analog_keys[0]; ++i)
        if (recomp_input_host_key(analog_keys[i].key)) g->analog_buttons[analog_keys[i].analog] = 0u;
    for (size_t i = 0; i < sizeof button_keys / sizeof button_keys[0]; ++i)
        if (recomp_input_host_key(button_keys[i].key)) g->buttons &= (uint16_t)~button_keys[i].button;
}

static bool any_key(void)
{
    for (int k = 0; k < RECOMP_INPUT_HOST_KEY_COUNT; ++k)
        if (recomp_input_host_key((RecompInputHostKey)k)) return true;
    return false;
}

/* Every pad and keyboard sample (input_host_win32.c), and once a frame from
   the freeze: the toggle and freeze keys, and while flying the camera's
   controls; the game is then handed a still pad. The stick clicks come
   apart from the pad, since the swimsuit switch keeps them from the game. */
static void capture(uint32_t port, RecompInputGamepad *g, uint16_t stick_clicks)
{
    if (port != 0u) {
        if (state.active) memset(g, 0, sizeof *g);
        return;
    }
    /* Not flying, the game still gets the keys' buttons. */
    RecompInputGamepad raw = *g;
    leave_out_keys(g);
    const bool r3 = (stick_clicks & XBOX_RIGHT_THUMB) != 0u;
    bool any_pad = g->buttons != 0u || stick_clicks != 0u || stick(g->thumb_lx) || stick(g->thumb_ly) ||
        stick(g->thumb_rx) || stick(g->thumb_ry);
    for (unsigned i = 0; i < 8u && !any_pad; ++i) any_pad = pressed(g, i);
    if (state.controller_hints < 0) {
        const char *deck = getenv("SteamDeck");
        state.controller_hints = deck != NULL && strcmp(deck, "1") == 0;
    }
    if (any_key()) state.controller_hints = 0;
    else if (any_pad) state.controller_hints = 1;
#define KEY(name) recomp_input_host_key(RECOMP_INPUT_HOST_KEY_##name)

    /* With the swimsuit menu open the pad is the menu's, R3 included
       (input_host_win32.c leaves the rest of it out already). */
    const bool r3_press = r3 && !state.r3_held && !recomp_costume_inject_menu_open();
    state.r3_held = r3;
    const bool toggle_keys = KEY(F1) || KEY(GRAVE);
    if ((toggle_keys && !state.toggle_held) || r3_press) set_active(!state.active);
    state.toggle_held = toggle_keys;

    /* P pauses anywhere; START only while flying, since the game pauses with it. */
    const bool freeze = KEY(P) || (state.active && (g->buttons & XBOX_START));
    if (freeze && !state.freeze_held) {
        const bool pause = !recomp_scene_freeze_active();
        recomp_scene_freeze_set(pause);
        if (!pause && !state.active) recomp_scene_freeze_set_overlay_hidden(false);
    }
    state.freeze_held = freeze;
    const bool paused = recomp_scene_freeze_active();

    const bool overlay = (state.active || paused) && (KEY(H) || (state.active && pressed(g, RECOMP_INPUT_ANALOG_BLACK)));
    if (overlay && !state.overlay_held) recomp_scene_freeze_set_overlay_hidden(!recomp_scene_freeze_overlay_hidden());
    state.overlay_held = overlay;

    /* A step press moves one frame; held, it steps every frame after a while. */
    const bool step = (state.active || paused) && (KEY(PERIOD) || (state.active && pressed(g, RECOMP_INPUT_ANALOG_WHITE)));
    const uint64_t now = SDL_GetTicks();
    if (step && !state.step_held) { recomp_scene_freeze_step(); state.step_since = now; }
    else if (step && now - state.step_since >= STEP_REPEAT_AFTER_MS) recomp_scene_freeze_step();
    state.step_held = step;

    /* F12 anywhere, the d-pad's right while flying (the right arrow, which
       also presses it, is left out above). */
    const bool shot = KEY(F12) || (state.active && (g->buttons & XBOX_DPAD_RIGHT));
    if (shot && !state.shot_held) recomp_screenshot_request();
    state.shot_held = shot;

    /* Sampled while the camera is away too, so a drag never spans it. */
    float dx, dy;
    const bool dragging = recomp_input_host_mouse_drag(&dx, &dy);
    if (!state.active) { *g = raw; return; }

    const bool cycle = KEY(C) || pressed(g, RECOMP_INPUT_ANALOG_X);
    if (cycle && !state.cycle_held) state.cycle_pending = true;
    state.cycle_held = cycle;
    const bool reset = KEY(R) || pressed(g, RECOMP_INPUT_ANALOG_Y);
    if (reset && !state.reset_held) state.reset_pending = true;
    state.reset_held = reset;
    /* \ or the d-pad's left (the left arrow, which also presses it, is left
       out above). */
    const bool aim = KEY(BACKSLASH) || (g->buttons & XBOX_DPAD_LEFT);
    if (aim && !state.aim_held) {
        state.aim_fighter = (int)(state.aim_next++ % FIGHTERS);
        state.chosen = (unsigned)state.aim_fighter;
        fprintf(stderr, "recomp free camera: turning to fighter %u\n", state.chosen);
    }
    state.aim_held = aim;

    /* Swimsuits: the d-pad's up / down or = / -. Held, it repeats after
       350 ms, faster the longer it is held. */
    const int dir = ((KEY(EQUALS) || (g->buttons & XBOX_DPAD_UP)) ? 1 : 0) -
        ((KEY(MINUS) || (g->buttons & XBOX_DPAD_DOWN)) ? 1 : 0);
    if (dir != 0) {
        int steps = 0;
        if (dir != state.suit_hold.dir) { state.suit_hold.since = state.suit_hold.last = now; steps = 1; }
        else if (now - state.suit_hold.since >= 350u && now - state.suit_hold.last >= 60u) {
            state.suit_hold.last = now;
            steps = now - state.suit_hold.since > 3500u ? 10 : now - state.suit_hold.since > 1500u ? 3 : 1;
        }
        if (steps) recomp_costume_inject_browse(state.chosen & 1u, dir * steps);
    }
    state.suit_hold.dir = dir;

    RecompFreeCameraControls c;
    memset(&c, 0, sizeof c);
    c.move[0] = stick(g->thumb_lx);
    c.move[2] = stick(g->thumb_ly);
    c.look[0] = stick(g->thumb_rx);
    c.look[1] = stick(g->thumb_ry);
    c.move[1] = (g->analog_buttons[RECOMP_INPUT_ANALOG_RTRIG] - g->analog_buttons[RECOMP_INPUT_ANALOG_LTRIG]) / 255.0f;
    c.fast = pressed(g, RECOMP_INPUT_ANALOG_A);
    c.slow = pressed(g, RECOMP_INPUT_ANALOG_B);
    if (KEY(D)) c.move[0] += 1.0f;
    if (KEY(A)) c.move[0] -= 1.0f;
    if (KEY(W)) c.move[2] += 1.0f;
    if (KEY(S)) c.move[2] -= 1.0f;
    if (KEY(E)) c.move[1] += 1.0f;
    if (KEY(Q)) c.move[1] -= 1.0f;
    if (KEY(RIGHT)) c.look[0] += 1.0f;
    if (KEY(LEFT)) c.look[0] -= 1.0f;
    if (KEY(UP)) c.look[1] += 1.0f;
    if (KEY(DOWN)) c.look[1] -= 1.0f;
    if (KEY(Z)) c.zoom += 1.0f;
    if (KEY(X)) c.zoom -= 1.0f;
    c.fast |= KEY(SHIFT);
    c.slow |= KEY(CONTROL);
#undef KEY
    /* A right-mouse drag over the game turns the camera, by the pixels moved. */
    if (dragging) {
        const float scale = MOUSE_RADIANS_PER_PIXEL * (state.camera.fov > 0.0f ? state.camera.fov : 1.0f);
        c.turn[0] += dx * scale;
        c.turn[1] -= dy * scale;
    }
    /* Turns from several samples in one frame add up until the camera steps. */
    c.turn[0] += state.controls.turn[0];
    c.turn[1] += state.controls.turn[1];
    state.controls = c;
    memset(g, 0, sizeof *g);
}

/* While frozen the game stops polling; the freeze calls this once a frame. */
static void frozen_frame(uint32_t frame)
{
    (void)frame;
    RecompInputGamepad pad;
    recomp_input_host_sample(0u, &pad);
    hint_panel();
}

static void store(uint32_t object, const float eye[3], const float target[3], float fov)
{
    recomp_guest_store(object + CAMERA_EYE, eye, 12);
    recomp_guest_store(object + CAMERA_TARGET, target, 12);
    recomp_guest_store(object + CAMERA_FOV, &fov, 4);
    recomp_guest_store(object + BUILD_EYE, eye, 12);
    recomp_guest_store(object + BUILD_TARGET, target, 12);
    recomp_guest_store(object + BUILD_FOV, &fov, 4);
    const float level = 0.0f;
    recomp_guest_store(object + BUILD_ROLL, &level, 4);
}

static bool begin_from(uint32_t object)
{
    float eye[3], target[3], fov;
    recomp_guest_load(eye, object + CAMERA_EYE, 12);
    recomp_guest_load(target, object + CAMERA_TARGET, 12);
    recomp_guest_load(&fov, object + CAMERA_FOV, 4);
    if (!recomp_free_camera_begin(&state.camera, eye, target, fov)) return false;
    state.camera.speed *= SPEED_SCALE;
    state.home = state.camera;
    state.started = true;
    state.object = object;
    state.stepped_seconds = seconds_now();
    fprintf(stderr, "recomp free camera: flying camera %u\n", (object - CAMERA_FIRST) / CAMERA_STRIDE);
    return true;
}

/* The next object built in the last frame after the one flown, if any. */
static uint32_t next_built(uint32_t object)
{
    const uint32_t index = (object - CAMERA_FIRST) / CAMERA_STRIDE;
    for (uint32_t k = 1; k <= CAMERA_COUNT; ++k) {
        const uint32_t i = (index + k) % CAMERA_COUNT;
        if (state.built_last & (1u << i)) return CAMERA_FIRST + i * CAMERA_STRIDE;
    }
    return object;
}

/* 0x0002A540 (Japan 0x00027720), the camera build: the object is in ESI
   (Japan: EAX). */
void recomp_free_camera_build(void (*original)(void))
{
    const uint32_t object = DOA3(recomp_runtime.registers.esi, recomp_runtime.registers.eax);
    if (!enabled()) {
        original();
        return;
    }
    ++state.builds;
    if (state.take_at && state.builds == state.take_at && !state.active) set_active(true);
    if (state.freeze_at && recomp_d3d_frame_adapter_swap_counter() >= state.freeze_at) {
        state.freeze_at = 0;
        fprintf(stderr, "recomp free camera: freezing at build %lu, frame %u\n", state.builds, recomp_d3d_frame_adapter_swap_counter());
        recomp_scene_freeze_set(true);
        const char *hide = getenv("RECOMP_FREE_CAMERA_HIDE");
        if (hide && *hide == '1') recomp_scene_freeze_set_overlay_hidden(true);
    }
    /* Test hook: RECOMP_FREE_CAMERA_TEST_AT=<frame> requests a screenshot,
       moves the chosen fighter's swimsuit on one and logs the panel's line. */
    static long test_at = -2;
    if (test_at == -2) { const char *t = getenv("RECOMP_FREE_CAMERA_TEST_AT"); test_at = t ? atol(t) : -1; }
    if (test_at >= 0 && recomp_d3d_frame_adapter_swap_counter() >= (uint32_t)test_at) {
        test_at = -1;
        recomp_screenshot_request();
        recomp_costume_inject_browse(state.chosen & 1u, 1);
        char line[RECOMP_OVERLAY_LINE_SIZE] = "";
        recomp_costume_inject_describe(state.chosen & 1u, line, sizeof line);
        fprintf(stderr, "recomp free camera: test screenshot requested; suit %s\n", line);
    }
    const bool ours = object >= CAMERA_FIRST && object < CAMERA_FIRST + CAMERA_COUNT * CAMERA_STRIDE &&
        (object - CAMERA_FIRST) % CAMERA_STRIDE == 0u;
    if (ours) {
        const uint32_t frame = recomp_d3d_frame_adapter_swap_counter();
        if (frame != state.built_frame) {
            /* The one flown was not built in the last frame that built any: take the next one built. */
            if (state.started && !(state.built_now & (1u << ((state.object - CAMERA_FIRST) / CAMERA_STRIDE)))) {
                state.started = false;
                state.object = 0;
            }
            state.built_last = state.built_now;
            state.built_now = 0;
            state.built_frame = frame;
        }
        state.built_now |= 1u << ((object - CAMERA_FIRST) / CAMERA_STRIDE);
    }
    if (state.active && ours && state.started && state.cycle_pending && object == state.object) {
        state.cycle_pending = false;
        const uint32_t next = next_built(object);
        if (next != object) { state.started = false; state.object = next; }
    }
    if (state.active && ours && (state.object == 0u || state.object == object)) {
        if (!state.started) begin_from(object);
        if (state.started && state.object == object) {
            if (state.reset_pending) { state.reset_pending = false; state.camera = state.home; }
            if (state.aim_fighter >= 0) {
                float hips[16];
                recomp_guest_load(hips, fighter_bones[state.aim_fighter], sizeof hips);
                if (isfinite(hips[12]) && (hips[12] != 0.0f || hips[13] != 0.0f || hips[14] != 0.0f))
                    recomp_free_camera_look_at(&state.camera, hips + 12);
                state.aim_fighter = -1;
            }
            const double now = seconds_now();
            double seconds = now - state.stepped_seconds;
            if (!(seconds > 0.0) || seconds > MAX_STEP_SECONDS) seconds = FRAME_SECONDS;
            state.stepped_seconds = now;
            recomp_free_camera_step(&state.camera, state.drifting ? &state.drift : &state.controls, (float)seconds);
            state.controls.turn[0] = state.controls.turn[1] = 0.0f;
            float target[3];
            recomp_free_camera_target(&state.camera, target);
            store(object, state.camera.position, target, state.camera.fov);
            if (!recomp_scene_freeze_active()) hint_panel();
        }
    }
    original();
}
#endif
