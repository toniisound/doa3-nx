#ifndef DOA3_RECOMP_FREE_CAMERA_ADAPTER_H
#define DOA3_RECOMP_FREE_CAMERA_ADAPTER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The camera build 0x0002A540 (object in ESI), wrapped from
   program_forwards.c: while RECOMP_FREE_CAMERA=1 flies a camera, its eye,
   target and field of view go in before the original build runs. */
void recomp_free_camera_build(void (*original)(void));
/* With RECOMP_FREE_CAMERA=1, hooks the input host (the camera's controls)
   and task switches (the scene freeze); false and nothing hooked otherwise. */
bool recomp_free_camera_install(void);
/* True while the free camera flies (its panel then has the overlay). */
bool recomp_free_camera_flying(void);

#ifdef __cplusplus
}
#endif

#endif
