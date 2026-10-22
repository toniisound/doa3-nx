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

bool recomp_input_host_sample(uint32_t port, RecompInputGamepad *gamepad)
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
    gamepad->thumb_rx = clamp_axis(right.x);
    gamepad->thumb_ry = clamp_axis(right.y);
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
