#ifndef DOA3_RECOMP_SUIT_LEGS_H
#define DOA3_RECOMP_SUIT_LEGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "suit_joints.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The back of the ankle when the toes come up further than DOAXBV's girls
   ever lift them (a fighter's back foot in a deep stance).

   In DOAXBV's model the skin over the Achilles is rigid: the ankle piece's
   lower back and the heel move with the foot, the lower calf with the shin.
   When the shin leans forward over a flat foot the calf's bottom edge swings
   forward and the heel stays back, so the back of the leg kinks into a hump.
   A real tendon is a straight cord from the calf to the heel bone.

   So, toes up only: every vertex behind the ankle point, between the heel
   bone's top (LOW below the ankle point along the shin) and the calf (HIGH
   above), is placed on its column's straight segment - from the column's
   point at LOW, where the foot puts it, to its point at HIGH, where the shin
   puts it - by its height. Columns run along the shin's axis at the ankle's
   rest pose (the shin to foot matrix at which the girl's own unwelded ankle
   mesh on the disc is the stored shape, fitted from its top ring). Vertices
   at one rest position move alike, so the pieces stay joined.

   A straight cord alone cuts in front of the ankle when the toes come far
   up, so above the ankle (fading in from 1 cm below it to 2 cm above) no
   vertex comes nearer the shin's line than it sits at the rest pose. The
   shin's line runs through the point the shin turns about relative to the
   foot; the heel below keeps its own shape.

   The cord comes in from GATE_FROM degrees of toes-up past the rest pose
   (DOA3's standing pose is about 11) over GATE_SPAN; toes down, standing and
   everything outside the band are drawn as the game draws them. */

enum {
    RECOMP_SUIT_LEG_LOW_MM = -60, RECOMP_SUIT_LEG_HIGH_MM = 160,
    RECOMP_SUIT_LEG_GATE_FROM = 12, RECOMP_SUIT_LEG_GATE_SPAN = 15,
};

/* One vertex the cord can move, in the foot's frame at the rest pose. */
typedef struct RecompSuitLegVertex {
    uint32_t vertex;          /* in its part */
    uint32_t follows_shin;    /* the game places it with the shin (else the foot) */
    float behind, height;     /* how far behind the ankle (0-1); 0-1 from LOW to HIGH */
    float rest[3], normal[3]; /* position and normal at the rest pose */
    float bottom[3], top[3];  /* its column's points at LOW (on the foot) and HIGH (on the shin) */
} RecompSuitLegVertex;

/* The ankle's rest pose on bone a (`foot`, 3 or 9) from the girl's geometry
   and joints entries: false if it cannot be fitted within 0.5 mm. */
bool recomp_suit_leg_rest(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t foot, float rest[16]);

/* A rest position and normal (foot frame) as a cord vertex; false if the
   cord never moves it (in front of the ankle, or outside LOW-HIGH). */
bool recomp_suit_leg_vertex(const float rest_pose[16], const float position[3], const float normal[3],
    bool follows_shin, RecompSuitLegVertex *out);

/* This frame's motion: `shin_in_foot` is the shin's matrix in the foot's
   frame (shin world times the inverse of foot world). Gives the motion since
   the rest pose (foot frame) and how far the cord is in (0-1). */
void recomp_suit_leg_motion(const float rest_pose[16], const float shin_in_foot[16], float motion[16], float *gate);

/* Where a cord vertex goes (foot frame). */
void recomp_suit_leg_place(const RecompSuitLegVertex *v, const float motion[16], float gate,
    float position[3], float normal[3]);

#ifdef __cplusplus
}
#endif

#endif
