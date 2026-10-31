#include "suit_skirt.h"

#include <math.h>
#include <string.h>

enum { MAX = RECOMP_SUIT_SKIRT_MAX };

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void point_by(const float p[3], const float m[16], float out[3])
{
    float r[3];
    for (unsigned c = 0; c < 3u; ++c) r[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
    memcpy(out, r, sizeof r);
}

static void direction_by(const float v[3], const float m[16], float out[3])
{
    float r[3];
    for (unsigned c = 0; c < 3u; ++c) r[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c];
    memcpy(out, r, sizeof r);
}

/* The inverse of a rigid row-vector matrix. */
static void invert_rigid(const float m[16], float out[16])
{
    float r[16] = {0};
    for (unsigned i = 0; i < 3u; ++i)
        for (unsigned j = 0; j < 3u; ++j) r[i * 4u + j] = m[j * 4u + i];
    for (unsigned c = 0; c < 3u; ++c) r[12 + c] = -(m[12] * r[c] + m[13] * r[4 + c] + m[14] * r[8 + c]);
    r[15] = 1.0f;
    memcpy(out, r, sizeof r);
}

void recomp_suit_skirt_rest_direction(const float root[3], float radial, float axial, float out[3])
{
    const float l = sqrtf(root[0] * root[0] + root[2] * root[2]);
    out[0] = l > 1e-6f ? radial * root[0] / l : 0.0f;
    out[1] = -axial;
    out[2] = l > 1e-6f ? radial * root[2] / l : 0.0f;
}

static void set_length(float v[3], float length)
{
    const float l = sqrtf(dot(v, v));
    if (l < 1e-9f) { v[0] = 0.0f; v[1] = -length; v[2] = 0.0f; return; }
    for (unsigned k = 0; k < 3u; ++k) v[k] *= length / l;
}

void recomp_suit_skirt_reset(RecompSuitSkirt *s, const RecompSuitSkirtShape *shape, const float bone[16],
    const float (*points)[3])
{
    memset(s, 0, sizeof *s);
    /* DOAXBV 0x0005B150: the velocity starts at its steady state under
       gravity, -(gravity / 2) / (1 - damping). */
    const float fall = shape->damping < 1.0f ? -0.5f * shape->gravity / (1.0f - shape->damping) : 0.0f;
    for (uint32_t c = 0; c < shape->cols; ++c) {
        float p[3], rest[3];
        point_by(shape->root[c], bone, p);
        direction_by(shape->rest[c], bone, rest);
        memcpy(s->point[c][0], p, sizeof p);
        for (uint32_t r = 0; r < shape->rows; ++r) {
            RecompSuitSkirtNode *n = &s->node[c][r];
            float link[3];
            if (points) {
                const float *q = points[c * shape->rows + r];
                for (unsigned k = 0; k < 3u; ++k) link[k] = q[k] - p[k];
            } else {
                memcpy(link, rest, sizeof link);
            }
            set_length(link, shape->length[c]);
            memcpy(n->parent, p, sizeof p);
            memcpy(n->link, link, sizeof link);
            n->velocity[1] = fall;
            for (unsigned k = 0; k < 3u; ++k) p[k] += link[k];
            memcpy(s->point[c][r + 1u], p, sizeof p);
        }
        memcpy(s->node[c][shape->rows].parent, p, sizeof p);
    }
    memcpy(s->bone, bone, sizeof s->bone);
    s->started = true;
}

static void carry_point(float p[3], const float delta[16], float share)
{
    float q[3];
    point_by(p, delta, q);
    for (unsigned k = 0; k < 3u; ++k) p[k] += share * (q[k] - p[k]);
}

void recomp_suit_skirt_carry(RecompSuitSkirt *s, const RecompSuitSkirtShape *shape, const float bone[16], float share)
{
    if (!s->started) return;
    /* The bone's motion since last tick: inverse(last) * now. */
    float inverse[16], delta[16];
    invert_rigid(s->bone, inverse);
    for (unsigned i = 0; i < 4u; ++i)
        for (unsigned j = 0; j < 4u; ++j)
            delta[i * 4u + j] = inverse[i * 4u] * bone[j] + inverse[i * 4u + 1u] * bone[4u + j] +
                inverse[i * 4u + 2u] * bone[8u + j] + inverse[i * 4u + 3u] * bone[12u + j];
    for (uint32_t c = 0; c < shape->cols; ++c)
        for (uint32_t r = 0; r <= shape->rows; ++r) {
            RecompSuitSkirtNode *n = &s->node[c][r];
            carry_point(n->parent, delta, share);
            if (r < shape->rows) {
                float l[3], v[3];
                direction_by(n->link, delta, l); direction_by(n->velocity, delta, v);
                for (unsigned k = 0; k < 3u; ++k) { n->link[k] += share * (l[k] - n->link[k]); n->velocity[k] += share * (v[k] - n->velocity[k]); }
            }
        }
    memcpy(s->bone, bone, sizeof s->bone);
}

/* Keeps the tip of a link above the floor at the link's length. */
static void clamp_floor(float link[3], const float root[3], float floor_y, float length)
{
    if (root[1] + link[1] >= floor_y) return;
    const float y = floor_y - root[1];
    if (fabsf(y) >= length) { link[0] = link[2] = 0.0f; link[1] = y < 0.0f ? -length : length; return; }
    const float across = sqrtf(link[0] * link[0] + link[2] * link[2]), want = sqrtf(length * length - y * y);
    if (across > 1e-9f) { link[0] *= want / across; link[2] *= want / across; } else link[0] = want;
    link[1] = y;
}

/* DOAXBV 0x0005EA10: two tips further apart than `spacing` (or apart from it
   either way when `both`) are moved half the difference each, along the
   line between them. */
static void hold_spacing(const float pa[3], float la[3], const float pb[3], float lb[3], float spacing, bool both)
{
    float d[3];
    for (unsigned k = 0; k < 3u; ++k) d[k] = (pa[k] + la[k]) - (pb[k] + lb[k]);
    float l = sqrtf(dot(d, d));
    if (!both && l <= spacing) return;
    if (l == 0.0f) l = 1.0f;
    const float f = (l - spacing) * 0.5f / l;
    for (unsigned k = 0; k < 3u; ++k) { la[k] -= d[k] * f; lb[k] += d[k] * f; }
}

/* The closest points between segments a-b and c-d: parameters s on a-b and t on c-d. */
static void closest(const float a[3], const float b[3], const float c[3], const float d[3], float *s, float *t)
{
    float u[3], v[3], w[3];
    for (unsigned k = 0; k < 3u; ++k) { u[k] = b[k] - a[k]; v[k] = d[k] - c[k]; w[k] = a[k] - c[k]; }
    const float A = dot(u, u), B = dot(u, v), C = dot(v, v), D = dot(u, w), E = dot(v, w), den = A * C - B * B;
    float sn = den > 1e-12f ? (B * E - C * D) / den : 0.0f;
    sn = sn < 0.0f ? 0.0f : sn > 1.0f ? 1.0f : sn;
    float tn = C > 1e-12f ? (B * sn + E) / C : 0.0f;
    if (tn < 0.0f) { tn = 0.0f; sn = A > 1e-12f ? -D / A : 0.0f; }
    else if (tn > 1.0f) { tn = 1.0f; sn = A > 1e-12f ? (B - D) / A : 0.0f; }
    *s = sn < 0.0f ? 0.0f : sn > 1.0f ? 1.0f : sn;
    *t = tn;
}

/* How far to move hem segment a-b (collider frame) to clear one shape;
   false when it is clear. Capsule (type 3): p0 its axis (0 x, 1 y, 2 z),
   p1 and p2 its centre on the other two axes in order, p3 its radius, p4..p5
   its extent along the axis (rounded ends). Sphere (4): centre p0..p2,
   radius p3. Ellipsoid (5): centre p0..p2, radii p3..p5. */
static bool clear_shape(const RecompSuitSkirtCollider *k, const float a[3], const float b[3], float push[3])
{
    float c0[3], c1[3], radius;
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float sa[3], sb[3];
    if (k->type == 3u) {
        const unsigned axis = (unsigned)k->p[0] > 2u ? 1u : (unsigned)k->p[0], o1 = axis == 0u ? 1u : 0u, o2 = axis == 2u ? 1u : 2u;
        c0[axis] = k->p[4]; c1[axis] = k->p[5];
        c0[o1] = c1[o1] = k->p[1]; c0[o2] = c1[o2] = k->p[2];
        radius = k->p[3];
    } else if (k->type == 4u || k->type == 5u) {
        memcpy(c0, k->p, 12); memcpy(c1, k->p, 12);
        radius = k->type == 4u ? k->p[3] : 1.0f;
        if (k->type == 5u)
            for (unsigned i = 0; i < 3u; ++i) scale[i] = k->p[3 + i] > 1e-6f ? k->p[3 + i] : 1e-6f;
    } else {
        return false;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        sa[i] = (a[i] - c0[i]) / scale[i] + c0[i];
        sb[i] = (b[i] - c0[i]) / scale[i] + c0[i];
    }
    float s, t, x[3], y[3], v[3];
    closest(sa, sb, c0, c1, &s, &t);
    for (unsigned i = 0; i < 3u; ++i) {
        x[i] = sa[i] + s * (sb[i] - sa[i]);
        y[i] = c0[i] + t * (c1[i] - c0[i]);
        v[i] = x[i] - y[i];
    }
    const float l = sqrtf(dot(v, v));
    if (l >= radius) return false;
    if (l < 1e-9f) { v[0] = 0.0f; v[1] = 0.0f; v[2] = 1.0f; }
    const float f = l < 1e-9f ? radius : (radius - l) / l;
    for (unsigned i = 0; i < 3u; ++i) push[i] = v[i] * f * scale[i];
    return true;
}

/* DOAXBV 0x00061520: each hem segment of the row (tip c-1 to tip c, round
   the ring) is moved out of each collider; both its tips move alike. */
static void collide_row(const RecompSuitSkirtShape *shape, const float (*bones)[16], float p[][3], float l[][3], bool closed)
{
    for (uint32_t k = 0; k < shape->collider_count; ++k) {
        const RecompSuitSkirtCollider *col = &shape->collider[k];
        if (col->bone >= 32u) continue;
        float inverse[16];
        invert_rigid(bones[col->bone], inverse);
        const uint32_t first = closed ? 0u : 1u;
        for (uint32_t c = first; c < shape->cols; ++c) {
            const uint32_t a = c ? c - 1u : shape->cols - 1u;
            float ta[3], tb[3], la[3], lb[3], push[3], world[3];
            for (unsigned i = 0; i < 3u; ++i) { ta[i] = p[a][i] + l[a][i]; tb[i] = p[c][i] + l[c][i]; }
            point_by(ta, inverse, la); point_by(tb, inverse, lb);
            if (!clear_shape(col, la, lb, push)) continue;
            direction_by(push, bones[col->bone], world);
            for (unsigned i = 0; i < 3u; ++i) { l[a][i] += world[i]; l[c][i] += world[i]; }
        }
    }
}

void recomp_suit_skirt_step(RecompSuitSkirt *s, const RecompSuitSkirtShape *shape, const float bone[16],
    const float (*bones)[16], float floor_y)
{
    if (!s->started) { recomp_suit_skirt_reset(s, shape, bone, NULL); return; }
    float p[MAX][3], rest[MAX][3], l[MAX][3];
    for (uint32_t c = 0; c < shape->cols; ++c) {
        point_by(shape->root[c], bone, p[c]);
        direction_by(shape->rest[c], bone, rest[c]);
        memcpy(s->point[c][0], p[c], 12);
    }
    const float fall = shape->gravity * 0.5f;
    float floor = floor_y + 0.002f;
    const bool closed = shape->ring_mode == 0u;
    const uint32_t split = shape->split < shape->cols ? shape->split : shape->cols / 2u;
    for (uint32_t r = 0; r < shape->rows; ++r) {
        for (uint32_t c = 0; c < shape->cols; ++c) {
            RecompSuitSkirtNode *n = &s->node[c][r];
            float d[3], parent_link[3], t[3];
            for (unsigned k = 0; k < 3u; ++k) {
                d[k] = n->parent[k] - p[c][k];
                n->velocity[k] = shape->damping * (n->velocity[k] + d[k] - n->pull[k]);
                n->pull[k] = d[k];
            }
            n->velocity[1] -= fall;
            for (unsigned k = 0; k < 3u; ++k)
                parent_link[k] = r == 0u ? rest[c][k] * shape->decay : s->node[c][r - 1u].link[k];
            for (unsigned k = 0; k < 3u; ++k)
                t[k] = shape->inertia * d[k] + n->velocity[k] + rest[c][k] + shape->bend * (parent_link[k] - n->link[k]);
            for (unsigned k = 0; k < 3u; ++k) rest[c][k] *= shape->decay;
            const float m = sqrtf(dot(t, t));
            if (m > shape->max_step) for (unsigned k = 0; k < 3u; ++k) t[k] *= shape->max_step / m;
            for (unsigned k = 0; k < 3u; ++k) l[c][k] = n->link[k] + t[k];
            set_length(l[c], shape->length[c]);
            clamp_floor(l[c], p[c], floor, shape->length[c]);
        }
        /* The ring: neighbours round a closed skirt; two open halves
           either side of the split for a wrap or slit skirt. */
        const bool both = (shape->flags & 0x10u) != 0u;
        for (uint32_t c = 0; c < shape->cols; ++c) {
            const uint32_t b = c + 1u;
            if (b == shape->cols) {
                if (closed) hold_spacing(p[c], l[c], p[0], l[0], shape->spacing[r], both);
            } else if (closed || b != split) {
                hold_spacing(p[c], l[c], p[b], l[b], shape->spacing[r], both);
            }
        }
        collide_row(shape, bones, p, l, closed);
        for (uint32_t c = 0; c < shape->cols; ++c) {
            RecompSuitSkirtNode *n = &s->node[c][r];
            memcpy(n->parent, p[c], 12);
            memcpy(n->link, l[c], 12);
            for (unsigned k = 0; k < 3u; ++k) p[c][k] += l[c][k];
            memcpy(s->point[c][r + 1u], p[c], 12);
        }
        floor -= 0.001f;
    }
    for (uint32_t c = 0; c < shape->cols; ++c) memcpy(s->node[c][shape->rows].parent, p[c], 12);
    memcpy(s->bone, bone, sizeof s->bone);
}
