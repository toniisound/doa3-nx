#ifndef RECOMP_WIDESCREEN_H
#define RECOMP_WIDESCREEN_H

#include <math.h>
#include <stdbool.h>
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
    char *end = NULL;
    unsigned long w = 0ul, h = 0ul;

    if (value == NULL || strcmp(value, "0") == 0) {
        return false;
    }
    w = strtoul(value, &end, 10);
    if (*end == ':') {
        h = strtoul(end + 1, &end, 10);
    }
    if (*end != '\0' || w > 16384ul || h > 16384ul || h == 0ul ||
        w * 3ul < h * 4ul || w > h * 4ul) {
        w = 16ul;
        h = 9ul;
    }
    *aspect_w = (unsigned)w;
    *aspect_h = (unsigned)h;
    return true;
}

/* DOA3 widens only its 3D view and its movies at 16:9; HUD, menu and
   subtitle quads keep 4:3 positions and would stretch by a third. A
   screen-space quad narrower than the centered 4:3 window is that 2D layer. */
static inline bool recomp_squeeze_widescreen_2d(float left, float right, float width)
{
    return left > width / 8.0f + 1.0f || right < width * 7.0f / 8.0f - 1.0f;
}

typedef struct RecompWidescreen2dScale {
    float x, y;
} RecompWidescreen2dScale;

/* How a widescreen screen-space quad scales about the screen center, for a
   guest target of width x height shown at aspect_w:aspect_h. The 4:3 2D
   layer narrows to 4:3 of the screen's shape (3/4 at 16:9, 5/6 at 16:10).
   Movies arrive in a centered 4:3 frame the game sized for 16:9, at least as
   tall as the screen: 540x480 guest pixels around 4:3 film, or a taller
   zoomed frame around letterboxed 16:9 film. The frame keeps its shape at
   full height, except that the zoomed frame shrinks to full width on a
   screen narrower than 16:9. Fades and backdrops keep their geometry. */
static inline RecompWidescreen2dScale recomp_widescreen_2d_scale(float width, float height,
    unsigned aspect_w, unsigned aspect_h, float left, float right, float top, float bottom)
{
    const float wide = 16.0f * aspect_h / (9.0f * aspect_w);
    const float frame = 0.75f * width / height; /* a 4:3 frame in 16:9 guest units */
    RecompWidescreen2dScale scale = {1.0f, 1.0f};

    if (fabsf(left + right - width) <= 2.0f && bottom - top >= height - 2.0f &&
        fabsf(right - left - frame * (bottom - top)) <= 0.01f * (right - left)) {
        if (bottom - top > height + 2.0f && wide > 1.0f) {
            scale.y = 1.0f / wide;
        } else {
            scale.x = wide;
        }
    } else if (recomp_squeeze_widescreen_2d(left, right, width)) {
        scale.x = 0.75f * wide;
    }
    return scale;
}

#endif
