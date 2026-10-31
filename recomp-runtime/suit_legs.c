#include "suit_legs.h"
#include "body_joints.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool recomp_suit_leg_rest(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t foot, float rest[16])
{
    uint32_t vbo;
    RecompSuitJoint ankle;
    if (!recomp_suit_joint_mesh_of(geometry, geometry_size, joints, joints_size, foot, &vbo) ||
        !recomp_suit_joint_parse(geometry, geometry_size, joints, joints_size, vbo, &ankle)) return false;
    /* The mesh's vertex count (OBJ +32), then the top ring (step 16): each
       group's stream vertex against its targets' mean, least squares. */
    uint32_t count = 0;
    for (size_t i = 0; i + 40u <= geometry_size && !count; ++i) {
        if (memcmp(geometry + i, "OBJ", 4) != 0) continue;
        uint32_t at; memcpy(&at, geometry + i + 36u, 4);
        if (at == vbo) memcpy(&count, geometry + i + 32u, 4);
    }
    double xtx[4][4] = {{0}}, xtp[4][3] = {{0}};
    uint32_t i = 0, stream = 0, ring = 0;
    const uint32_t *w = ankle.words;
    while (count && i < ankle.word_count && w[i] != 0u) {
        const uint32_t step = w[i++];
        for (;;) {
            double p[3] = {0, 0, 0}; unsigned n = 0;
            for (; i < ankle.word_count && w[i] != RECOMP_SUIT_JOINT_END; ++i) {
                if (w[i] >= count || (size_t)vbo + (size_t)(w[i] + 1u) * 32u > geometry_size) continue;
                float q[3]; memcpy(q, geometry + vbo + (size_t)w[i] * 32u, 12);
                for (unsigned c = 0; c < 3u; ++c) p[c] += q[c];
                ++n;
            }
            ++i;
            if (step == 16u && n && stream < ankle.stream_count) {
                const float *q = ankle.stream + (size_t)stream * 8u;
                const double x[4] = {q[0], q[1], q[2], 1.0};
                for (unsigned r = 0; r < 4u; ++r) {
                    for (unsigned c = 0; c < 4u; ++c) xtx[r][c] += x[r] * x[c];
                    for (unsigned c = 0; c < 3u; ++c) xtp[r][c] += x[r] * p[c] / n;
                }
                ++ring;
            }
            ++stream;
            if (i < ankle.word_count && w[i] == RECOMP_SUIT_JOINT_END) { ++i; break; }
        }
    }
    /* Solve the 4x4 normal equations (Gaussian elimination, partial pivots). */
    bool ok = ring >= 6u;
    for (int c = 0; c < 4 && ok; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r) if (fabs(xtx[r][c]) > fabs(xtx[pivot][c])) pivot = r;
        if (fabs(xtx[pivot][c]) < 1e-12) { ok = false; break; }
        for (int k = 0; k < 4; ++k) { double t = xtx[c][k]; xtx[c][k] = xtx[pivot][k]; xtx[pivot][k] = t; }
        for (int k = 0; k < 3; ++k) { double t = xtp[c][k]; xtp[c][k] = xtp[pivot][k]; xtp[pivot][k] = t; }
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = xtx[r][c] / xtx[c][c];
            for (int k = 0; k < 4; ++k) xtx[r][k] -= f * xtx[c][k];
            for (int k = 0; k < 3; ++k) xtp[r][k] -= f * xtp[c][k];
        }
    }
    if (ok) {
        memset(rest, 0, 64);
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) rest[r * 4 + c] = (float)(xtp[r][c] / xtx[r][r]);
        rest[15] = 1.0f;
        /* The fit must put the ring where the mesh has it. */
        float worst = 0.0f;
        i = 0; stream = 0;
        while (i < ankle.word_count && w[i] != 0u) {
            const uint32_t step = w[i++];
            for (;;) {
                float placed[4] = {0};
                if (step == 16u && stream < ankle.stream_count) recomp_joint_transform(rest, ankle.stream + (size_t)stream * 8u, placed);
                for (; i < ankle.word_count && w[i] != RECOMP_SUIT_JOINT_END; ++i) {
                    if (step != 16u || w[i] >= count || (size_t)vbo + (size_t)(w[i] + 1u) * 32u > geometry_size) continue;
                    float q[3]; memcpy(q, geometry + vbo + (size_t)w[i] * 32u, 12);
                    for (unsigned c = 0; c < 3u; ++c) worst = fmaxf(worst, fabsf(q[c] - placed[c]));
                }
                ++i; ++stream;
                if (i < ankle.word_count && w[i] == RECOMP_SUIT_JOINT_END) { ++i; break; }
            }
        }
        ok = worst <= 0.0005f;
    }
    recomp_suit_joint_free(&ankle);
    return ok;
}

static void affine_inverse(const float m[16], float out[16])
{
    const float *a = m;
    const double det = a[0]*(a[5]*a[10]-a[6]*a[9]) - a[1]*(a[4]*a[10]-a[6]*a[8]) + a[2]*(a[4]*a[9]-a[5]*a[8]);
    const double inv = det != 0.0 ? 1.0 / det : 0.0;
    float r[16] = {0};
    r[0] = (float)((a[5]*a[10]-a[6]*a[9])*inv); r[1] = (float)((a[2]*a[9]-a[1]*a[10])*inv); r[2] = (float)((a[1]*a[6]-a[2]*a[5])*inv);
    r[4] = (float)((a[6]*a[8]-a[4]*a[10])*inv); r[5] = (float)((a[0]*a[10]-a[2]*a[8])*inv); r[6] = (float)((a[2]*a[4]-a[0]*a[6])*inv);
    r[8] = (float)((a[4]*a[9]-a[5]*a[8])*inv); r[9] = (float)((a[1]*a[8]-a[0]*a[9])*inv); r[10] = (float)((a[0]*a[5]-a[1]*a[4])*inv);
    for (unsigned c = 0; c < 3; ++c) r[12+c] = -(m[12]*r[c] + m[13]*r[4+c] + m[14]*r[8+c]);
    r[15] = 1;
    memcpy(out, r, sizeof r);
}

static void point(const float p[3], const float m[16], float out[3])
{
    float r[3];
    for (unsigned c = 0; c < 3u; ++c) r[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
    memcpy(out, r, sizeof r);
}

static void direction(const float n[3], const float m[16], float out[3])
{
    float r[3];
    for (unsigned c = 0; c < 3u; ++c) r[c] = n[0] * m[c] + n[1] * m[4 + c] + n[2] * m[8 + c];
    memcpy(out, r, sizeof r);
}

/* The shin's axis (the rest pose's y axis, in the foot's frame), unit. */
static bool shin_axis(const float rest_pose[16], float axis[3])
{
    const float l = sqrtf(rest_pose[4] * rest_pose[4] + rest_pose[5] * rest_pose[5] + rest_pose[6] * rest_pose[6]);
    if (!(l > 0.0f)) return false;
    for (unsigned c = 0; c < 3u; ++c) axis[c] = rest_pose[4 + c] / l;
    return true;
}

bool recomp_suit_leg_vertex(const float rest_pose[16], const float position[3], const float normal[3],
    bool follows_shin, RecompSuitLegVertex *out)
{
    float axis[3];
    if (!shin_axis(rest_pose, axis)) return false;
    const float low = RECOMP_SUIT_LEG_LOW_MM / 1000.0f, high = RECOMP_SUIT_LEG_HIGH_MM / 1000.0f;
    /* Behind the ankle point: the foot's z is forward; full 2 cm behind,
       none 1 cm in front. */
    float behind = (0.01f - position[2]) / 0.03f;
    behind = behind < 0.0f ? 0.0f : behind > 1.0f ? 1.0f : behind;
    const float h = position[0] * axis[0] + position[1] * axis[1] + position[2] * axis[2];
    if (!(behind > 0.0f) || !(h > low) || !(h < high)) return false;
    out->follows_shin = follows_shin;
    out->behind = behind;
    out->height = (h - low) / (high - low);
    for (unsigned c = 0; c < 3u; ++c) {
        out->rest[c] = position[c];
        out->normal[c] = normal[c];
        out->bottom[c] = position[c] - (h - low) * axis[c];
        out->top[c] = position[c] + (high - h) * axis[c];
    }
    return true;
}

/* The shin's lean forward in the foot's frame (degrees, + = toes up). */
static float lean(const float shin_in_foot[16])
{
    return atan2f(shin_in_foot[6], shin_in_foot[5]) * (180.0f / 3.14159265f);
}

void recomp_suit_leg_motion(const float rest_pose[16], const float shin_in_foot[16], float motion[16], float *gate)
{
    float inverse[16];
    affine_inverse(rest_pose, inverse);
    float r[16];
    for (unsigned i = 0; i < 4u; ++i)
        for (unsigned j = 0; j < 4u; ++j) {
            double s = 0;
            for (unsigned k = 0; k < 4u; ++k) s += (double)inverse[i * 4u + k] * shin_in_foot[k * 4u + j];
            r[i * 4u + j] = (float)s;
        }
    memcpy(motion, r, sizeof r);
    float g = (lean(shin_in_foot) - lean(rest_pose) - (float)RECOMP_SUIT_LEG_GATE_FROM) / (float)RECOMP_SUIT_LEG_GATE_SPAN;
    *gate = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
}

void recomp_suit_leg_place(const RecompSuitLegVertex *v, const float motion[16], float gate,
    float position[3], float normal[3])
{
    /* Where the game puts it now, and its place on the straight cord. */
    float game[3], game_normal[3], top[3], shin_normal[3];
    if (v->follows_shin) { point(v->rest, motion, game); direction(v->normal, motion, game_normal); }
    else { memcpy(game, v->rest, 12); memcpy(game_normal, v->normal, 12); }
    point(v->top, motion, top);
    direction(v->normal, motion, shin_normal);
    const float t = v->height, k = gate * v->behind;
    float n[3];
    for (unsigned c = 0; c < 3u; ++c) {
        const float cord = (1.0f - t) * v->bottom[c] + t * top[c];
        const float cord_normal = (1.0f - t) * v->normal[c] + t * shin_normal[c];
        position[c] = game[c] + k * (cord - game[c]);
        n[c] = game_normal[c] + k * (cord_normal - game_normal[c]);
    }
    /* The cord alone cuts in front of the ankle when the toes come far up
       (half the ankle's depth gone by 50 degrees). A tendon runs along the
       shinbone's back, not through it: above the ankle the back stays at
       least as far behind the shin's line as it sits at the rest pose. The shin's line passes through the
       point the shin turns about (the motion's fixed point in the foot's
       y-z plane, p = p R + t), along the column (top - bottom) turned by
       the motion. Behind is the side away from the foot's +z. */
    const float a11 = 1.0f - motion[5], a12 = -motion[9], a21 = -motion[6], a22 = 1.0f - motion[10];
    const float det = a11 * a22 - a12 * a21;
    if (k > 0.0f && fabsf(det) > 1e-4f) {
        const float cy = (motion[13] * a22 - a12 * motion[14]) / det, cz = (a11 * motion[14] - a21 * motion[13]) / det;
        float rest_axis[3] = {v->top[0] - v->bottom[0], v->top[1] - v->bottom[1], v->top[2] - v->bottom[2]}, now_axis[3];
        direction(rest_axis, motion, now_axis);
        const float lr = sqrtf(rest_axis[1] * rest_axis[1] + rest_axis[2] * rest_axis[2]);
        const float ln = sqrtf(now_axis[1] * now_axis[1] + now_axis[2] * now_axis[2]);
        if (lr > 1e-6f && ln > 1e-6f) {
            /* The y-z normal to the line that points back (-z for an upright shin). */
            const float br[2] = {rest_axis[2] / lr, -rest_axis[1] / lr}, bn[2] = {now_axis[2] / ln, -now_axis[1] / ln};
            const float d_rest = (v->rest[1] - cy) * br[0] + (v->rest[2] - cz) * br[1];
            const float d_now = (position[1] - cy) * bn[0] + (position[2] - cz) * bn[1];
            /* Above the ankle only: the heel keeps its own round shape. */
            const float h = RECOMP_SUIT_LEG_LOW_MM / 1000.0f + v->height * (RECOMP_SUIT_LEG_HIGH_MM - RECOMP_SUIT_LEG_LOW_MM) / 1000.0f;
            float up = (h + 0.01f) / 0.03f;
            up = up < 0.0f ? 0.0f : up > 1.0f ? 1.0f : up;
            up = up * up * (3.0f - 2.0f * up);
            if (d_rest > 0.0f && d_now < d_rest) {
                position[1] += up * k * (d_rest - d_now) * bn[0];
                position[2] += up * k * (d_rest - d_now) * bn[1];
            }
        }
    }
    const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (unsigned c = 0; c < 3u; ++c) normal[c] = l > 0.0f ? n[c] / l : n[c];
}
