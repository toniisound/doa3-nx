/* suit_skirt.c on a made-up skirt: fourteen strands of four links round a
   still hip hang down and stay at their lengths; a capsule thigh pushed
   into the front of the hem moves the hem out of it; carrying the skirt
   with the hip moves it along. */
#include "suit_skirt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static float distance(const float a[3], const float b[3])
{
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

int main(void)
{
    static RecompSuitSkirtShape shape;
    shape.bone = 2; shape.cols = 14; shape.rows = 4;
    shape.gravity = 0.01f; shape.inertia = 0.4f; shape.damping = 0.6f; shape.decay = 0.5f; shape.bend = 0.1f; shape.max_step = 0.05f;
    for (unsigned c = 0; c < 14u; ++c) {
        const float a = 6.2831853f * (float)c / 14.0f;
        shape.root[c][0] = 0.1f * sinf(a); shape.root[c][1] = 0.05f; shape.root[c][2] = 0.1f * cosf(a);
        shape.length[c] = 0.05f;
        recomp_suit_skirt_rest_direction(shape.root[c], 0.005f, 0.02f, shape.rest[c]);
    }
    for (unsigned r = 0; r < 4u; ++r) shape.spacing[r] = 0.06f;
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, bones[32][16];
    for (unsigned i = 0; i < 32u; ++i) memcpy(bones[i], identity, sizeof identity);

    static RecompSuitSkirt s;
    recomp_suit_skirt_reset(&s, &shape, identity, NULL);
    for (unsigned t = 0; t < 300u; ++t) recomp_suit_skirt_step(&s, &shape, identity, (const float (*)[16])bones, -1e9f);
    /* Hanging: every link points down and keeps its length. */
    for (unsigned c = 0; c < 14u; ++c)
        for (unsigned r = 1; r <= 4u; ++r) {
            CHECK(s.point[c][r][1] < s.point[c][r - 1u][1] - 0.04f);
            CHECK(fabsf(distance(s.point[c][r], s.point[c][r - 1u]) - 0.05f) < 1e-3f);
        }

    /* A thigh capsule (bone 6, along y, radius 0.06) through the front of the hem. */
    shape.collider_count = 1;
    shape.collider[0].bone = 6; shape.collider[0].type = 3;
    const float capsule[6] = {1.0f, 0.0f, 0.0f, 0.06f, -0.2f, 0.0f};
    memcpy(shape.collider[0].p, capsule, sizeof capsule);
    bones[6][14] = 0.09f;               /* centre z 0.09: inside the front strands */
    for (unsigned t = 0; t < 120u; ++t) recomp_suit_skirt_step(&s, &shape, identity, (const float (*)[16])bones, -1e9f);
    const float axis[3] = {0.0f, s.point[0][4][1], 0.09f};
    CHECK(distance(s.point[0][4], axis) > 0.055f);

    /* Carried with the hip: a whole-body move of 1 m takes the skirt along. */
    float moved[16];
    memcpy(moved, identity, sizeof moved);
    moved[12] = 1.0f;
    shape.collider_count = 0;
    const float before = s.point[3][4][0];
    recomp_suit_skirt_carry(&s, &shape, moved, 1.0f);
    recomp_suit_skirt_step(&s, &shape, moved, (const float (*)[16])bones, -1e9f);
    CHECK(fabsf(s.point[3][4][0] - before - 1.0f) < 0.02f);

    if (failures) return 1;
    puts("suit skirt: ok");
    return 0;
}
