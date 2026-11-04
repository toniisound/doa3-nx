#ifndef DOAXBV_RECOMP_INPUT_HOST_WIN32_H
#define DOAXBV_RECOMP_INPUT_HOST_WIN32_H

#include "input_model.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool recomp_input_host_sample(uint32_t port, RecompInputGamepad *gamepad);

void recomp_input_host_set_vibration(
    uint32_t port,
    uint16_t left_motor,
    uint16_t right_motor);
void recomp_input_host_stop_vibration(void);
/* Optional hook on each pad sample. While it is set the stick clicks (L3,
   R3) never reach the game; when it returns true (a combo or menu) the face
   buttons, shoulder buttons, triggers, d-pad, Start and sticks are kept from
   the game too. */
typedef bool (*RecompInputComboHook)(uint32_t port, const RecompInputGamepad *gamepad);
void recomp_input_host_set_combo_hook(RecompInputComboHook hook);
/* Sees every finished sample (pad and keyboard) and may change it: the free
   camera reads its controls here and hands the game a still pad.
   stick_clicks holds the pad's L3 and R3 bits, which the combo hook keeps
   from the game. */
typedef void (*RecompInputCaptureHook)(uint32_t port, RecompInputGamepad *gamepad, uint16_t stick_clicks);
void recomp_input_host_set_capture_hook(RecompInputCaptureHook hook);

/* Keys the runtime reads for itself. Some (A S Z X Q W E R, the arrows,
   Return, Backspace) also press pad buttons for the game; the free camera
   leaves those buttons out while it flies. */
typedef enum RecompInputHostKey {
    RECOMP_INPUT_HOST_KEY_F1,
    RECOMP_INPUT_HOST_KEY_GRAVE,
    RECOMP_INPUT_HOST_KEY_F12,
    RECOMP_INPUT_HOST_KEY_W,
    RECOMP_INPUT_HOST_KEY_A,
    RECOMP_INPUT_HOST_KEY_S,
    RECOMP_INPUT_HOST_KEY_D,
    RECOMP_INPUT_HOST_KEY_Q,
    RECOMP_INPUT_HOST_KEY_E,
    RECOMP_INPUT_HOST_KEY_R,
    RECOMP_INPUT_HOST_KEY_Z,
    RECOMP_INPUT_HOST_KEY_X,
    RECOMP_INPUT_HOST_KEY_C,
    RECOMP_INPUT_HOST_KEY_P,
    RECOMP_INPUT_HOST_KEY_H,
    RECOMP_INPUT_HOST_KEY_PERIOD,
    RECOMP_INPUT_HOST_KEY_BACKSLASH,
    RECOMP_INPUT_HOST_KEY_MINUS,
    RECOMP_INPUT_HOST_KEY_EQUALS,
    RECOMP_INPUT_HOST_KEY_UP,
    RECOMP_INPUT_HOST_KEY_DOWN,
    RECOMP_INPUT_HOST_KEY_LEFT,
    RECOMP_INPUT_HOST_KEY_RIGHT,
    RECOMP_INPUT_HOST_KEY_RETURN,
    RECOMP_INPUT_HOST_KEY_BACKSPACE,
    RECOMP_INPUT_HOST_KEY_SHIFT,
    RECOMP_INPUT_HOST_KEY_CONTROL,
    RECOMP_INPUT_HOST_KEY_COUNT
} RecompInputHostKey;

/* False while the game window does not have the keyboard. */
bool recomp_input_host_key(RecompInputHostKey key);
/* True while a right-button drag that started over the game is held (it goes
   on outside the window), with the pointer's motion in pixels since the last
   call; zero on the call that starts a drag. */
bool recomp_input_host_mouse_drag(float *dx, float *dy);

#ifdef __cplusplus
}
#endif

#endif
