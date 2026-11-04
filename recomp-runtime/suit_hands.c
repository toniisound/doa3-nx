#include "suit_hands.h"

#include <stdlib.h>
#include <string.h>

static bool word_at(const uint8_t *p, size_t size, size_t at, uint32_t *out)
{
    if (at + 4u > size) return false;
    memcpy(out, p + at, 4);
    return true;
}

bool recomp_suit_hand_shapes(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t vbo, float **fist, float **flat, uint32_t *count)
{
    *fist = *flat = NULL; *count = 0;
    /* The mesh: its OBJ index, vertex count and stored positions. */
    uint32_t mesh = 0, nv = 0;
    bool found = false;
    for (size_t i = 0; i + 40u <= geometry_size && !found; ++i) {
        if (memcmp(geometry + i, "OBJ", 4) != 0) continue;
        uint32_t at;
        memcpy(&at, geometry + i + 36u, 4);
        if (at == vbo) { memcpy(&nv, geometry + i + 32u, 4); found = true; }
        else ++mesh;
    }
    if (!found || nv == 0u || (size_t)vbo + (size_t)nv * 32u > geometry_size) return false;

    /* Its morph set. */
    uint32_t section, sets;
    if (!word_at(joints, joints_size, 12u, &section) || (size_t)section + 8u > joints_size ||
        memcmp(joints + section, "apv", 4) != 0 || !word_at(joints, joints_size, section + 4u, &sets)) return false;
    uint32_t set[7] = {0};
    found = false;
    for (uint32_t s = 0; s < sets && s < 64u && !found; ++s) {
        uint32_t offset;
        if (!word_at(joints, joints_size, section + 8u + 4u * s, &offset) || (size_t)section + offset + 28u > joints_size) return false;
        memcpy(set, joints + section + offset, sizeof set);
        found = (set[1] & 0x7FFFFFFFu) == mesh;
    }
    const uint32_t shapes = set[0], morphed = set[2];
    /* Bit 31: the shapes are the other hand's, mirrored in x. */
    const float mirror = (set[1] & 0x80000000u) ? -1.0f : 1.0f;
    if (!found || shapes == 0u || shapes > 256u || morphed == 0u || morphed > nv ||
        (size_t)section + set[5] + 4u * morphed > joints_size) return false;
    uint32_t *index = malloc(4u * morphed);
    float *base = malloc(12u * nv), *shape = malloc(12u * nv);
    *fist = calloc(nv, 12u); *flat = calloc(nv, 12u);
    bool ok = index && base && shape && *fist && *flat;
    if (ok) {
        memcpy(index, joints + section + set[5], 4u * morphed);
        for (uint32_t m = 0; m < morphed && ok; ++m) { ok = index[m] % 32u == 0u && index[m] / 32u < nv; index[m] /= 32u; }
        for (uint32_t v = 0; v < nv; ++v) memcpy(base + 3u * v, geometry + vbo + 32u * v, 12);
    }
    /* The fist and the flat hand: the shortest and longest along x. */
    uint32_t shape_at[256], fist_k = 0, flat_k = 0;
    float shortest = 1e9f, longest = -1e9f;
    for (uint32_t k = 0; k < shapes && ok; ++k) {
        ok = word_at(joints, joints_size, (size_t)section + set[4] + 8u * k, &shape_at[k]) &&
            (size_t)section + shape_at[k] + 12u * morphed <= joints_size;
        if (!ok) break;
        memcpy(shape, base, 12u * nv);
        for (uint32_t m = 0; m < morphed; ++m) {
            memcpy(shape + 3u * index[m], joints + section + shape_at[k] + 12u * m, 12);
            shape[3u * index[m]] *= mirror;
        }
        float lo = 1e9f, hi = -1e9f;
        for (uint32_t v = 0; v < nv; ++v) { lo = shape[3u * v] < lo ? shape[3u * v] : lo; hi = shape[3u * v] > hi ? shape[3u * v] : hi; }
        if (hi - lo < shortest) { shortest = hi - lo; fist_k = k; }
        if (hi - lo > longest) { longest = hi - lo; flat_k = k; }
    }
    for (unsigned which = 0; which < 2u && ok; ++which) {
        float *into = which ? *flat : *fist;
        const uint32_t at = shape_at[which ? flat_k : fist_k];
        for (uint32_t m = 0; m < morphed; ++m) {
            float p[3];
            memcpy(p, joints + section + at + 12u * m, 12);
            p[0] *= mirror;
            for (unsigned c = 0; c < 3u; ++c) into[3u * index[m] + c] = p[c] - base[3u * index[m] + c];
        }
    }
    free(index); free(base); free(shape);
    if (!ok) { free(*fist); free(*flat); *fist = *flat = NULL; return false; }
    *count = nv;
    return true;
}
