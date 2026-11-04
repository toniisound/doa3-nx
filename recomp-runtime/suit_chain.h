#ifndef DOA3_RECOMP_SUIT_CHAIN_H
#define DOA3_RECOMP_SUIT_CHAIN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DOAXBV's chains: tassels, ties and strings, as DOAXBV moves them.

   A chain hangs from a root point on one bone (a tassel from the cup's
   edge on the chest). Every 60 Hz tick each link swings as a damped
   pendulum: it keeps INERTIA of its direction as its parent moves, picks
   up the parent's acceleration, falls, and is pushed by a rest force that
   fades down the chain; then it is kept at its length and its tip is
   pushed out of the chain's capsules. Each node then carries a small
   frame (along the chain, rolled about the bone's x axis) and places its
   group of the mesh's vertices at their offsets in it.

   Every number is the suit's own, read from its script entry on the disc:
   DOAXBV 0x00057300 (step), 0x00058740 (reset), 0x0005FEE0 and 0x00060710
   (collision, capsule) and 0x00057CB0 (vertices). Constants are per tick.
   Matrices are row-vector (point' = point * M), bone frames DOAXBV's. */

enum { RECOMP_SUIT_CHAIN_NODES = 16, RECOMP_SUIT_CHAIN_COLLIDERS = 8 };

/* A capsule (DOAXBV's chain shape 3) in a bone's frame: its axis (0 x,
   1 y, 2 z), its centre across the axis (axis 0: z then y; axis 1: x then
   z; axis 2: x then y), radius, and extent along the axis. */
typedef struct RecompSuitChainCollider { uint32_t bone, axis; float a, b, radius, low, high; } RecompSuitChainCollider;

typedef struct RecompSuitChainShape {
    uint32_t bone, nodes;
    float length, gravity, inertia, damping, decay, follow;
    float root[3], force[3]; /* bone frame */
    uint32_t collider_count;
    RecompSuitChainCollider collider[RECOMP_SUIT_CHAIN_COLLIDERS];
} RecompSuitChainShape;

/* A mesh vertex a node places: at `point` in the node's frame. */
typedef struct RecompSuitChainVertex { uint32_t node, part, vertex; float point[3]; } RecompSuitChainVertex;

typedef struct RecompSuitChainNode { float parent[3], link[3], velocity[3], pull[3]; } RecompSuitChainNode;

typedef struct RecompSuitChain {
    bool started;
    float bone[16]; /* the chain bone's matrix last tick */
    RecompSuitChainNode node[RECOMP_SUIT_CHAIN_NODES];
} RecompSuitChain;

/* Starts the chain at rest: each link along its rest force and fall (straight down without a force). */
void recomp_suit_chain_reset(RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16],
    const float (*bones)[16], float floor_y);

/* Carries the chain with its bone's motion since last tick, by `share` (see recomp_suit_skirt_carry). */
void recomp_suit_chain_carry(RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16], float share);

/* One tick; `bones` gives each collider's bone world matrix. */
void recomp_suit_chain_step(RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16],
    const float (*bones)[16], float floor_y);

/* The node frames in the bone's frame (DOAXBV 0x00057CB0): frame[i] is
   node i's (rows: across, back along the chain, roll axis, the node's tip). */
void recomp_suit_chain_frames(const RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16],
    float frame[][16]);

#ifdef __cplusplus
}
#endif

#endif
