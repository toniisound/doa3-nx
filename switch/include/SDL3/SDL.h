/* SPDX-License-Identifier: GPL-3.0-or-later
   The few SDL3 calls the runtime's C modules make (costume_inject.c,
   free_camera_adapter.c) for desktop keyboard shortcuts. The Switch has no
   keyboard: every key reads up, and SDL_GetTicks counts milliseconds. */
#ifndef DOA3_NX_SDL_SHIM_H
#define DOA3_NX_SDL_SHIM_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum SDL_Scancode {
    SDL_SCANCODE_F = 9,
    SDL_SCANCODE_G = 10,
    SDL_SCANCODE_1 = 30,
    SDL_SCANCODE_2 = 31,
    SDL_SCANCODE_F5 = 62,
    SDL_SCANCODE_F6 = 63,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_COUNT = 512
} SDL_Scancode;

static inline const bool *SDL_GetKeyboardState(int *numkeys)
{
    static const bool keys[SDL_SCANCODE_COUNT];
    if (numkeys) *numkeys = SDL_SCANCODE_COUNT;
    return keys;
}

static inline uint64_t SDL_GetTicks(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

#endif
