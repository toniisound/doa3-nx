#ifndef RECOMP_WIDESCREEN_H
#define RECOMP_WIDESCREEN_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RECOMP_D3D_WIDESCREEN: unset or 0 is the dashboard's 4:3 setting. Any other
   value reports its widescreen flag; "W:H" from 4:3 to 4:1, such as 16:10
   or a monitor's 3440:1440, also gives the screen's shape, and anything else
   means 16:9. The kernel, the runner and both presenters read it here so
   they agree. */
static inline bool recomp_widescreen(unsigned *aspect_w, unsigned *aspect_h)
{
    const char *value = getenv("RECOMP_D3D_WIDESCREEN");
    unsigned w = 0u, h = 0u;

    if (value == NULL || strcmp(value, "0") == 0) {
        return false;
    }
    if (sscanf(value, "%u:%u", &w, &h) != 2 || w > 16384u || h > 16384u ||
        w * 3u < h * 4u || w > h * 4u || h == 0u) {
        w = 16u;
        h = 9u;
    }
    *aspect_w = w;
    *aspect_h = h;
    return true;
}

#endif
