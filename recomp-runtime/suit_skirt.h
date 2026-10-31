#ifndef DOA3_RECOMP_SUIT_SKIRT_H
#define DOA3_RECOMP_SUIT_SKIRT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DOAXBV's skirts, as DOAXBV moves them.

   A skirt is a curtain: COLS strands hang from a ring of root points on one
   bone (the hip), each ROWS links long. Every 60 Hz tick each link swings
   as a damped pendulum (it keeps its direction as its parent moves, picks
   up the parent's acceleration, falls, and is pushed toward a flared rest
   direction and toward its parent link), is kept at its length, the ring
   of tips is held at its spacing when stretched, and the hem is pushed out
   of ellipsoids, capsules and spheres on the hip, waist and thighs. The
   grid's points then replace the skirt mesh's vertices, row by row; the
   waistband (the root ring) keeps its skinning.

   Every number is the suit's own, read from its script entry on the disc
   (the entry after its geometry): DOAXBV 0x0005ADF0 (init), 0x0005B150
   (reset), 0x0005B9E0 (step), 0x0005EA10 (ring), 0x00061520 and
   0x00060DC0/0x00060FB0/0x00061150 (colliders). Constants are per tick;
   there is no time step. Positions are metres, bone frames are DOAXBV's,
   matrices are row-vector (point' = point * M). */

enum { RECOMP_SUIT_SKIRT_MAX = 25, RECOMP_SUIT_SKIRT_COLLIDERS = 16 };

typedef struct RecompSuitSkirtCollider {
    uint32_t bone, type; /* 3 capsule, 4 sphere, 5 ellipsoid */
    float p[6];          /* in the bone's frame; see suit_skirt.c */
} RecompSuitSkirtCollider;

typedef struct RecompSuitSkirtShape {
    uint32_t bone, ring_mode, split, flags, cols, rows;
    float gravity, inertia, damping, decay, bend, max_step;
    float spacing[RECOMP_SUIT_SKIRT_MAX];               /* per row: the ring's rest spacing */
    float length[RECOMP_SUIT_SKIRT_MAX];                /* per strand: its links' length */
    float root[RECOMP_SUIT_SKIRT_MAX][3];               /* per strand: its root, bone frame */
    float rest[RECOMP_SUIT_SKIRT_MAX][3];               /* per strand: its flared rest direction, bone frame */
    uint32_t collider_count;
    RecompSuitSkirtCollider collider[RECOMP_SUIT_SKIRT_COLLIDERS];
} RecompSuitSkirtShape;

/* A mesh vertex a grid point places: at the point plus `offset` (the
   skirt bone's frame). */
typedef struct RecompSuitSkirtVertex { uint32_t point, part, vertex; float offset[3]; } RecompSuitSkirtVertex;

typedef struct RecompSuitSkirtNode { float parent[3], link[3], velocity[3], pull[3]; } RecompSuitSkirtNode;

typedef struct RecompSuitSkirt {
    bool started;
    float bone[16]; /* the skirt bone's matrix last tick */
    RecompSuitSkirtNode node[RECOMP_SUIT_SKIRT_MAX][RECOMP_SUIT_SKIRT_MAX + 1];
    float point[RECOMP_SUIT_SKIRT_MAX][RECOMP_SUIT_SKIRT_MAX + 1][3]; /* world: row 0 the roots, row r the r-th tips */
} RecompSuitSkirt;

/* The rest direction of a strand from its radial and downward reach (record
   +0x1C, +0x20) and its root: out from the bone's axis (y) by `radial`,
   down by `axial`. */
void recomp_suit_skirt_rest_direction(const float root[3], float radial, float axial, float out[3]);

/* Starts the skirt at rest. `points` (strand-major: strand c, rows 1..rows,
   world) gives the links their starting directions, from the mesh as it
   was authored; NULL hangs them along their rest directions. */
void recomp_suit_skirt_reset(RecompSuitSkirt *s, const RecompSuitSkirtShape *shape, const float bone[16],
    const float (*points)[3]);

/* Carries the skirt with its bone's motion since last tick, by `share`
   (0-1): the part of the bone's world motion the skirt does not feel as
   inertia. DOAXBV's own girls never move faster than a stroll; a DOA3
   fighter dashes ten times as fast, and the full inertia would fling the
   skirt flat. */
void recomp_suit_skirt_carry(RecompSuitSkirt *s, const RecompSuitSkirtShape *shape, const float bone[16], float share);

/* One tick. `bones` gives each collider's bone world matrix (index by the
   collider's `bone`); `floor_y` the ground height. */
void recomp_suit_skirt_step(RecompSuitSkirt *s, const RecompSuitSkirtShape *shape, const float bone[16],
    const float (*bones)[16], float floor_y);

#ifdef __cplusplus
}
#endif

#endif
