#include "suit_joints.h"
#include "body_joints.h"

#include <stdlib.h>
#include <string.h>

enum { FRAME_BYTES = 0xA0u, VERTEX_BYTES = 32u, MAX_DESCRIPTORS = 32u, MAX_WORDS = 4096u };

static bool word_at(const uint8_t *p, size_t size, size_t at, uint32_t *out)
{
    if (at + 4u > size) return false;
    memcpy(out, p + at, 4);
    return true;
}

/* The OBJ index (in file order) of the mesh whose vertices start at vbo. */
static bool mesh_index(const uint8_t *geometry, size_t size, uint32_t vbo, uint32_t *index)
{
    uint32_t n = 0;
    for (size_t i = 0; i + 40u <= size; ++i) {
        if (memcmp(geometry + i, "OBJ", 4) != 0) continue;
        uint32_t at;
        memcpy(&at, geometry + i + 36u, 4);
        if (at == vbo) { *index = n; return true; }
        ++n;
    }
    return false;
}

bool recomp_suit_joint_parse(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t vbo, RecompSuitJoint *out)
{
    memset(out, 0, sizeof *out);
    uint32_t mesh, section;
    if (!mesh_index(geometry, geometry_size, vbo, &mesh) || !word_at(joints, joints_size, 16u, &section)) return false;
    for (uint32_t d = 0; d < MAX_DESCRIPTORS; ++d) {
        uint32_t offset;
        if (!word_at(joints, joints_size, (size_t)section + 4u * d, &offset) || offset == 0u) return false;
        const size_t at = (size_t)section + offset;
        if (at + 20u > joints_size || joints[at + 8u] != mesh) continue;
        uint32_t list, data;
        memcpy(&list, joints + at, 4);
        memcpy(&data, joints + at + 4u, 4);
        const uint8_t a = joints[at + 9u], flags = joints[at + 11u];
        uint8_t b = joints[at + 10u];
        if (flags & 0x30u) return false;
        if (a == 4u && b == 8u) b = 30u;
        if (a == 10u && b == 14u) b = 31u;
        out->bone_a = a;
        out->bone_b = b;

        /* The interior list, its byte offsets made vertex indices. */
        out->words = malloc(MAX_WORDS * 4u);
        if (!out->words) return false;
        size_t w = (size_t)section + list;
        uint32_t n = 0, groups = 0, word;
        bool ok = true;
#define TAKE(v) (ok = ok && n < MAX_WORDS && word_at(joints, joints_size, w, &(v)), w += 4u, ok)
        while (TAKE(word) && word != 0u) {
            out->words[n++] = word;                                   /* step */
            for (;;) {
                while (TAKE(word) && word != RECOMP_SUIT_JOINT_END) {
                    if (word % VERTEX_BYTES) { ok = false; break; }
                    out->words[n++] = word / VERTEX_BYTES;
                }
                if (!ok) break;
                out->words[n++] = RECOMP_SUIT_JOINT_END;
                ++groups;
                if (!TAKE(word)) break;
                if (word == RECOMP_SUIT_JOINT_END) { out->words[n++] = word; break; }
                w -= 4u;                                                /* the next group's first target */
            }
            if (!ok) break;
        }
#undef TAKE
        if (!ok || n >= MAX_WORDS) { recomp_suit_joint_free(out); return false; }
        out->words[n++] = 0u;
        out->word_count = n;

        const size_t frame = (size_t)section + data;
        out->stream_count = groups;
        out->stream = malloc((size_t)groups * VERTEX_BYTES + 4u);
        if (!out->stream || frame + FRAME_BYTES + (size_t)groups * VERTEX_BYTES > joints_size) {
            recomp_suit_joint_free(out);
            return false;
        }
        memcpy(out->frame, joints + frame, FRAME_BYTES);
        memcpy(out->stream, joints + frame + FRAME_BYTES, (size_t)groups * VERTEX_BYTES);
        return true;
    }
    return false;
}

bool recomp_suit_joint_mesh_of(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t bone_a, uint32_t *vbo)
{
    uint32_t section;
    if (!word_at(joints, joints_size, 16u, &section)) return false;
    for (uint32_t d = 0; d < MAX_DESCRIPTORS; ++d) {
        uint32_t offset;
        if (!word_at(joints, joints_size, (size_t)section + 4u * d, &offset) || offset == 0u) return false;
        const size_t at = (size_t)section + offset;
        if (at + 20u > joints_size || joints[at + 9u] != bone_a || (joints[at + 11u] & 0x08u)) continue;
        uint32_t n = 0;
        for (size_t i = 0; i + 40u <= geometry_size; ++i) {
            if (memcmp(geometry + i, "OBJ", 4) != 0) continue;
            if (n++ == joints[at + 8u]) { memcpy(vbo, geometry + i + 36u, 4); return true; }
        }
        return false;
    }
    return false;
}

bool recomp_suit_joint_select(const RecompSuitJoint *joint, const uint32_t *own, uint32_t own_count, RecompSuitJoint *out)
{
    *out = *joint;
    out->words = malloc((size_t)joint->word_count * 4u + 4u);
    out->stream = malloc((size_t)joint->stream_count * VERTEX_BYTES + 4u);
    if (!out->words || !out->stream) { recomp_suit_joint_free(out); return false; }
    memcpy(out->stream, joint->stream, (size_t)joint->stream_count * VERTEX_BYTES);
    /* Walk the list as the bend does, re-indexing targets only. */
    uint32_t i = 0;
    const uint32_t *w = joint->words;
    while (i < joint->word_count && w[i] != 0u) {
        out->words[i] = w[i]; ++i;                                    /* step */
        for (;;) {
            while (i < joint->word_count && w[i] != RECOMP_SUIT_JOINT_END) {
                uint32_t to = RECOMP_SUIT_JOINT_SKIP;
                for (uint32_t k = 0; k < own_count; ++k) if (own[k] == w[i]) { to = k; break; }
                out->words[i++] = to;
            }
            out->words[i] = RECOMP_SUIT_JOINT_END; ++i;
            if (i < joint->word_count && w[i] == RECOMP_SUIT_JOINT_END) { out->words[i] = w[i]; ++i; break; }
        }
    }
    out->words[i] = 0u;
    return true;
}

void recomp_suit_joint_mirror(RecompSuitJoint *joint, const float m[16])
{
    float t[4];
    for (unsigned i = 0; i < 3u; ++i) {                  /* axes a_i, b_i: directions */
        for (unsigned k = 0; k < 2u; ++k) {
            float *v = joint->frame + i * 8u + k * 4u;
            recomp_joint_rotate(m, v, t);
            memcpy(v, t, 12);
        }
    }
    for (unsigned k = 0; k < 4u; ++k) {                  /* origins: points */
        float *v = joint->frame + 24u + k * 4u;
        recomp_joint_transform(m, v, t);
        memcpy(v, t, 12);
    }
    for (uint32_t s = 0; s < joint->stream_count; ++s) {
        float *v = joint->stream + s * 8u;
        recomp_joint_transform(m, v, t); memcpy(v, t, 12);
        recomp_joint_rotate(m, v + 4, t); memcpy(v + 4, t, 12);
    }
}

void recomp_suit_joint_free(RecompSuitJoint *joint)
{
    free(joint->words);
    free(joint->stream);
    joint->words = NULL;
    joint->stream = NULL;
}

/* DOAXBV's 0x00067630, interior list only (as every girl's joints are): the
   frames from the frame data, then each step's frame (a blend for steps
   below 16; the current matrix, which starts as `relative`, otherwise)
   places the next stream vertex into each target of a group. */
void recomp_suit_joint_bend(const RecompSuitJoint *joint, const float relative[16], uint8_t *vertices,
    uint32_t stride, uint32_t normal_offset, uint32_t count)
{
    float current[16], frames[64] = {0};
    memcpy(current, relative, sizeof current);
    recomp_joint_frames(current, joint->frame, frames);
    const uint32_t *w = joint->words;
    uint32_t i = 0, stream = 0;
    while (i < joint->word_count && w[i] != 0u) {
        const uint32_t step = w[i++];
        if (step < 16u) {
            float weights[4];
            recomp_joint_weights(step, weights);
            recomp_joint_blend_frame(frames, weights, current);
        }
        for (;;) {
            float position[4], normal[4];
            if (stream < joint->stream_count)
                recomp_joint_place(current, joint->stream + (size_t)stream * 8u, position, normal);
            ++stream;
            for (; i < joint->word_count && w[i] != RECOMP_SUIT_JOINT_END; ++i) {
                if (w[i] >= count) continue;
                uint8_t *v = vertices + (size_t)w[i] * stride;
                memcpy(v, position, 12);
                memcpy(v + normal_offset, normal, 12);
            }
            ++i;
            if (i >= joint->word_count || w[i] == RECOMP_SUIT_JOINT_END) { ++i; break; }  /* .dxc lists are unchecked */
        }
    }
}
