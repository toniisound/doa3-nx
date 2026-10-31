/* suit_chain.c on a made-up tassel: four links from a root on a still
   bone hang straight down at their lengths; a capsule where the tassel
   hangs pushes it out; the node frames are the identity rotation when the
   chain hangs straight. */
#include "suit_chain.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    static RecompSuitChainShape shape;
    shape.bone = 0; shape.nodes = 4; shape.length = 0.03f;
    shape.gravity = 0.02f; shape.inertia = 0.8f; shape.damping = 0.3f; shape.decay = 0.4f;
    shape.root[0] = 0.05f; shape.root[1] = 0.0f; shape.root[2] = 0.1f;
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, bones[32][16], frame[RECOMP_SUIT_CHAIN_NODES][16];
    for (unsigned i = 0; i < 32u; ++i) memcpy(bones[i], identity, sizeof identity);

    static RecompSuitChain c;
    recomp_suit_chain_reset(&c, &shape, identity, (const float (*)[16])bones, -1e9f);
    for (unsigned t = 0; t < 200u; ++t) recomp_suit_chain_step(&c, &shape, identity, (const float (*)[16])bones, -1e9f);
    for (unsigned i = 0; i < 4u; ++i) {
        const float *l = c.node[i].link;
        CHECK(fabsf(sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]) - 0.03f) < 1e-4f);
        CHECK(l[1] < -0.0299f);
    }
    recomp_suit_chain_frames(&c, &shape, identity, frame);
    CHECK(fabsf(frame[3][0] - 1.0f) < 1e-3f && fabsf(frame[3][5] - 1.0f) < 1e-3f && fabsf(frame[3][10] - 1.0f) < 1e-3f);
    CHECK(fabsf(frame[3][13] + 0.12f) < 1e-3f);

    /* A vertical capsule (axis y, centre x 0.05 z 0.08, radius 0.04) round the hanging tassel. */
    shape.collider_count = 1;
    RecompSuitChainCollider *k = &shape.collider[0];
    k->bone = 0; k->axis = 1; k->a = 0.05f; k->b = 0.08f; k->radius = 0.04f; k->low = -0.2f; k->high = -0.02f;
    for (unsigned t = 0; t < 60u; ++t) recomp_suit_chain_step(&c, &shape, identity, (const float (*)[16])bones, -1e9f);
    recomp_suit_chain_frames(&c, &shape, identity, frame);
    const float dx = frame[3][12] - 0.05f, dz = frame[3][14] - 0.08f;
    CHECK(sqrtf(dx * dx + dz * dz) > 0.039f);

    if (failures) return 1;
    puts("suit chain: ok");
    return 0;
}
