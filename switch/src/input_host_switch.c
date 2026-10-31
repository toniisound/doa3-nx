/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Nintendo Switch input host: libnx pads instead of SDL3 gamepads.
   Implements input_host_win32.h for the four Xbox ports.

   Face buttons map by position, as the SDL host does: the bottom button (B
   on a Switch pad) is Xbox A, right (A) is Xbox B, left (Y) is Xbox X and
   top (X) is Xbox Y. L/R are Black/White, ZL/ZR the analog triggers, + is
   START and - is BACK. Port 0 also accepts the handheld controls. */
#include "input_host_win32.h"
#include "input_pulse_source.h"

#include <switch.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define NX_PORT_COUNT 4u

/* XInput wButtons bits, as input_host_win32.c defines them. */
enum {
    XBOX_DPAD_UP = 0x0001u,
    XBOX_DPAD_DOWN = 0x0002u,
    XBOX_DPAD_LEFT = 0x0004u,
    XBOX_DPAD_RIGHT = 0x0008u,
    XBOX_START = 0x0010u,
    XBOX_BACK = 0x0020u,
    XBOX_LEFT_THUMB = 0x0040u,
    XBOX_RIGHT_THUMB = 0x0080u,
};

static PadState pads[NX_PORT_COUNT];
static bool initialized;
static HidVibrationDeviceHandle vibration_handles[NX_PORT_COUNT][2];
static bool vibration_ready[NX_PORT_COUNT];

static void initialize(void)
{
    if (initialized) return;
    initialized = true;
    padConfigureInput(NX_PORT_COUNT, HidNpadStyleSet_NpadStandard);
    for (uint32_t port = 0u; port < NX_PORT_COUNT; ++port) {
        if (port == 0u) {
            /* Player 1 is a docked pad or the console in handheld mode. */
            padInitialize(&pads[port], HidNpadIdType_No1, HidNpadIdType_Handheld);
        } else {
            padInitialize(&pads[port], (HidNpadIdType)(HidNpadIdType_No1 + port));
        }
    }
}

static int16_t clamp_axis(int32_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < -INT16_MAX) return -INT16_MAX;
    return (int16_t)value;
}

static bool sample_pad(uint32_t port, RecompInputGamepad *gamepad)
{
    if (gamepad == NULL || port >= RECOMP_INPUT_PORT_COUNT || port >= NX_PORT_COUNT) {
        return false;
    }
    initialize();
    memset(gamepad, 0, sizeof *gamepad);

    PadState *pad = &pads[port];
    padUpdate(pad);
    if (!padIsConnected(pad)) {
        /* Port 0 stays plugged in, as the keyboard keeps it on desktop hosts. */
        return port == 0u;
    }

    const u64 held = padGetButtons(pad);
    static const struct { u64 key; uint16_t mask; } digital[] = {
        {HidNpadButton_Up, XBOX_DPAD_UP},
        {HidNpadButton_Down, XBOX_DPAD_DOWN},
        {HidNpadButton_Left, XBOX_DPAD_LEFT},
        {HidNpadButton_Right, XBOX_DPAD_RIGHT},
        {HidNpadButton_Plus, XBOX_START},
        {HidNpadButton_Minus, XBOX_BACK},
        {HidNpadButton_StickL, XBOX_LEFT_THUMB},
        {HidNpadButton_StickR, XBOX_RIGHT_THUMB},
    };
    for (size_t i = 0; i < sizeof digital / sizeof digital[0]; ++i) {
        if ((held & digital[i].key) != 0) gamepad->buttons |= digital[i].mask;
    }

    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_A] = (held & HidNpadButton_B) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_B] = (held & HidNpadButton_A) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_X] = (held & HidNpadButton_Y) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_Y] = (held & HidNpadButton_X) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_BLACK] = (held & HidNpadButton_L) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_WHITE] = (held & HidNpadButton_R) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_LTRIG] = (held & HidNpadButton_ZL) ? 0xffu : 0u;
    gamepad->analog_buttons[RECOMP_INPUT_ANALOG_RTRIG] = (held & HidNpadButton_ZR) ? 0xffu : 0u;

    /* libnx sticks already point up for positive Y, as XInput does. */
    const HidAnalogStickState left = padGetStickPos(pad, 0);
    const HidAnalogStickState right = padGetStickPos(pad, 1);
    gamepad->thumb_lx = clamp_axis(left.x);
    gamepad->thumb_ly = clamp_axis(left.y);
    /* DOA3 moves with the D-pad only, so the left stick also presses it: eight
       45-degree sectors past a 35% deadzone, so diagonals (crouch-forward and
       the like) are as easy as on the pad. The stick axes still reach the game. */
    {
        const int32_t x = left.x, y = left.y;
        const int64_t deadzone = 32767 * 35 / 100;
        if ((int64_t)x * x + (int64_t)y * y > deadzone * deadzone) {
            const int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
            /* tan(22.5 deg) ~= 0.4142 */
            if ((int64_t)ax * 10000 > (int64_t)ay * 4142) {
                gamepad->buttons |= x > 0 ? XBOX_DPAD_RIGHT : XBOX_DPAD_LEFT;
            }
            if ((int64_t)ay * 10000 > (int64_t)ax * 4142) {
                gamepad->buttons |= y > 0 ? XBOX_DPAD_UP : XBOX_DPAD_DOWN;
            }
        }
    }
    gamepad->thumb_rx = clamp_axis(right.x);
    gamepad->thumb_ry = clamp_axis(right.y);
    return true;
}

static RecompInputComboHook combo_hook;
static RecompInputCaptureHook capture_hook;

void recomp_input_host_set_combo_hook(RecompInputComboHook hook) { combo_hook = hook; }
void recomp_input_host_set_capture_hook(RecompInputCaptureHook hook) { capture_hook = hook; }

/* No keyboard or mouse on the Switch. */
bool recomp_input_host_key(RecompInputHostKey key)
{
    (void)key;
    return false;
}

bool recomp_input_host_mouse_drag(float *dx, float *dy)
{
    if (dx) *dx = 0.0f;
    if (dy) *dy = 0.0f;
    return false;
}

bool recomp_input_host_sample(uint32_t port, RecompInputGamepad *gamepad)
{
    if (!sample_pad(port, gamepad)) return false;
    const uint16_t stick_clicks = gamepad->buttons & (XBOX_LEFT_THUMB | XBOX_RIGHT_THUMB);
    if (combo_hook != NULL) {
        /* While a combo hook is set the stick clicks stay with the runtime;
           a combo or menu keeps the rest of the pad from the game as well. */
        const bool held_back = combo_hook(port, gamepad);
        gamepad->buttons &= (uint16_t)~(XBOX_LEFT_THUMB | XBOX_RIGHT_THUMB);
        if (held_back) {
            gamepad->buttons = 0u;
            memset(gamepad->analog_buttons, 0, sizeof gamepad->analog_buttons);
            gamepad->thumb_lx = gamepad->thumb_ly = gamepad->thumb_rx = gamepad->thumb_ry = 0;
        }
    }
    if (capture_hook != NULL) capture_hook(port, gamepad, stick_clicks);
    return true;
}

static bool prepare_vibration(uint32_t port)
{
    if (vibration_ready[port]) return true;
    const HidNpadIdType id = port == 0u && padIsHandheld(&pads[0])
        ? HidNpadIdType_Handheld : (HidNpadIdType)(HidNpadIdType_No1 + port);
    const HidNpadStyleTag style = id == HidNpadIdType_Handheld
        ? HidNpadStyleTag_NpadHandheld : HidNpadStyleTag_NpadFullKey;
    if (R_FAILED(hidInitializeVibrationDevices(vibration_handles[port], 2, id, style))) {
        return false;
    }
    vibration_ready[port] = true;
    return true;
}

void recomp_input_host_set_vibration(uint32_t port, uint16_t left_motor, uint16_t right_motor)
{
    if (port >= NX_PORT_COUNT) return;
    initialize();
    if (!padIsConnected(&pads[port]) || !prepare_vibration(port)) return;
    /* The Xbox left motor is the heavy low-frequency one. */
    HidVibrationValue values[2];
    const float low = left_motor / 65535.0f;
    const float high = right_motor / 65535.0f;
    for (int i = 0; i < 2; ++i) {
        values[i].amp_low = low * 0.8f;
        values[i].freq_low = 160.0f;
        values[i].amp_high = high * 0.8f;
        values[i].freq_high = 320.0f;
    }
    hidSendVibrationValues(vibration_handles[port], values, 2);
}

void recomp_input_host_stop_vibration(void)
{
    for (uint32_t port = 0u; port < NX_PORT_COUNT; ++port) {
        if (vibration_ready[port]) recomp_input_host_set_vibration(port, 0u, 0u);
    }
}
