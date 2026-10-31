#include "suit_chain.h"

#include <math.h>
#include <string.h>

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

static void invert_rigid(const float m[16], float out[16])
{
    float r[16] = {0};
    for (unsigned i = 0; i < 3u; ++i)
        for (unsigned j = 0; j < 3u; ++j) r[i * 4u + j] = m[j * 4u + i];
    for (unsigned c = 0; c < 3u; ++c) r[12 + c] = -(m[12] * r[c] + m[13] * r[4 + c] + m[14] * r[8 + c]);
    r[15] = 1.0f;
    memcpy(out, r, sizeof r);
}

static void set_length(float v[3], float length)
{
    const float l = sqrtf(dot(v, v));
    if (l > 0.0f) for (unsigned k = 0; k < 3u; ++k) v[k] *= length / l;
}

/* DOAXBV 0x00060710: a tip inside the capsule is pushed straight out of it
   (round ends past its extent). */
static bool clear_capsule(const RecompSuitChainCollider *k, float t[3])
{
    const unsigned ax = k->axis > 2u ? 1u : k->axis;
    const unsigned ia = ax == 0u ? 2u : 0u, ib = ax == 2u ? 1u : ax == 0u ? 1u : 2u;
    const float da = t[ia] - k->a, db = t[ib] - k->b;
    const float e = t[ax] > k->high ? t[ax] - k->high : t[ax] < k->low ? t[ax] - k->low : 0.0f;
    const float d = sqrtf(e * e + da * da + db * db);
    if (d >= k->radius) return false;
    const float s = d > 0.0f ? (k->radius - d) / d : 1.0f;
    t[ax] += e * s; t[ia] += da * s; t[ib] += db * s;
    return true;
}

/* DOAXBV 0x0005FEE0: the link's tip against each capsule in its bone's
   frame; if any moved it, the link points at the new tip, at its length. */
static void collide(const RecompSuitChainShape *shape, const float (*bones)[16], const float p[3], float link[3])
{
    float t[3];
    bool hit = false;
    for (unsigned k = 0; k < 3u; ++k) t[k] = p[k] + link[k];
    for (uint32_t i = 0; i < shape->collider_count; ++i) {
        const RecompSuitChainCollider *k = &shape->collider[i];
        if (k->bone >= 32u) continue;
        float inverse[16], local[3];
        invert_rigid(bones[k->bone], inverse);
        point_by(t, inverse, local);
        if (!clear_capsule(k, local)) continue;
        point_by(local, bones[k->bone], t);
        hit = true;
    }
    if (!hit) return;
    for (unsigned k = 0; k < 3u; ++k) link[k] = t[k] - p[k];
    set_length(link, shape->length);
}

/* DOAXBV 0x0005E850: a tip below the floor is put on it at the link's length. */
static void clamp_floor(float link[3], float parent_y, float floor_y, float length)
{
    if (parent_y + link[1] >= floor_y) return;
    link[1] = 0.0f;
    if (link[0] == 0.0f && link[2] == 0.0f) link[0] = 1.0f;
    const float h = floor_y - parent_y;
    if (h > length) { link[0] = link[2] = 0.0f; link[1] = length; return; }
    const float across = sqrtf(link[0] * link[0] + link[2] * link[2]), want = sqrtf(length * length - h * h);
    link[0] *= want / across; link[2] *= want / across; link[1] = h;
}

void recomp_suit_chain_reset(RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16],
    const float (*bones)[16], float floor_y)
{
    memset(c, 0, sizeof *c);
    const float g = shape->gravity * 0.5f;
    const float fall = shape->damping >= 0.0f && shape->damping < 1.0f ? -g / (1.0f - shape->damping) : -g;
    float p[3], f[3], previous[3];
    point_by(shape->root, bone, p);
    direction_by(shape->force, bone, f);
    memcpy(previous, f, sizeof previous);
    const float floor = floor_y + 0.003f;
    for (uint32_t i = 0; i < shape->nodes && i < RECOMP_SUIT_CHAIN_NODES; ++i) {
        RecompSuitChainNode *n = &c->node[i];
        n->velocity[1] = fall;
        for (unsigned k = 0; k < 3u; ++k) {
            const float start = f[k] + n->velocity[k];
            n->link[k] = start + shape->follow * (previous[k] - start);
        }
        set_length(n->link, shape->length);
        for (unsigned k = 0; k < 3u; ++k) f[k] *= shape->decay;
        collide(shape, bones, p, n->link);
        clamp_floor(n->link, p[1], floor, shape->length);
        memcpy(n->parent, p, sizeof p);
        memcpy(previous, n->link, sizeof previous);
        for (unsigned k = 0; k < 3u; ++k) p[k] += n->link[k];
    }
    memcpy(c->bone, bone, sizeof c->bone);
    c->started = true;
}

void recomp_suit_chain_carry(RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16], float share)
{
    if (!c->started) return;
    float inverse[16], delta[16];
    invert_rigid(c->bone, inverse);
    for (unsigned i = 0; i < 4u; ++i)
        for (unsigned j = 0; j < 4u; ++j)
            delta[i * 4u + j] = inverse[i * 4u] * bone[j] + inverse[i * 4u + 1u] * bone[4u + j] +
                inverse[i * 4u + 2u] * bone[8u + j] + inverse[i * 4u + 3u] * bone[12u + j];
    for (uint32_t i = 0; i < shape->nodes && i < RECOMP_SUIT_CHAIN_NODES; ++i) {
        RecompSuitChainNode *n = &c->node[i];
        float q[3], l[3], v[3];
        point_by(n->parent, delta, q);
        direction_by(n->link, delta, l);
        direction_by(n->velocity, delta, v);
        for (unsigned k = 0; k < 3u; ++k) {
            n->parent[k] += share * (q[k] - n->parent[k]);
            n->link[k] += share * (l[k] - n->link[k]);
            n->velocity[k] += share * (v[k] - n->velocity[k]);
        }
    }
    memcpy(c->bone, bone, sizeof c->bone);
}

void recomp_suit_chain_step(RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16],
    const float (*bones)[16], float floor_y)
{
    if (!c->started) { recomp_suit_chain_reset(c, shape, bone, bones, floor_y); return; }
    float p[3], f[3];
    point_by(shape->root, bone, p);
    direction_by(shape->force, bone, f);
    const float g = shape->gravity * 0.5f, floor = floor_y + 0.003f;
    for (uint32_t i = 0; i < shape->nodes && i < RECOMP_SUIT_CHAIN_NODES; ++i) {
        RecompSuitChainNode *n = &c->node[i];
        float d[3], v[3];
        for (unsigned k = 0; k < 3u; ++k) {
            d[k] = n->parent[k] - p[k];
            v[k] = (n->velocity[k] + d[k] - n->pull[k]) * shape->damping;
        }
        v[1] -= g;                                /* gravity is not damped */
        memcpy(n->pull, d, sizeof d);
        for (unsigned k = 0; k < 3u; ++k) n->link[k] += d[k] * shape->inertia + v[k] + f[k];
        memcpy(n->velocity, v, sizeof v);
        for (unsigned k = 0; k < 3u; ++k) f[k] *= shape->decay;
        set_length(n->link, shape->length);
        collide(shape, bones, p, n->link);
        clamp_floor(n->link, p[1], floor, shape->length);
        memcpy(n->parent, p, sizeof p);
        for (unsigned k = 0; k < 3u; ++k) p[k] += n->link[k];
    }
    memcpy(c->bone, bone, sizeof c->bone);
}

void recomp_suit_chain_frames(const RecompSuitChain *c, const RecompSuitChainShape *shape, const float bone[16],
    float frame[][16])
{
    float inverse[16], link[RECOMP_SUIT_CHAIN_NODES][3], q[3];
    const uint32_t nodes = shape->nodes < RECOMP_SUIT_CHAIN_NODES ? shape->nodes : RECOMP_SUIT_CHAIN_NODES;
    invert_rigid(bone, inverse);
    for (uint32_t i = 0; i < nodes; ++i) direction_by(c->node[i].link, inverse, link[i]);
    memcpy(q, shape->root, sizeof q);
    for (uint32_t i = 0; i < nodes; ++i) {
        float y[3], x[3], z[3];
        for (unsigned k = 0; k < 3u; ++k) { q[k] += link[i][k]; y[k] = -(link[i][k] + (i + 1u < nodes ? link[i + 1u][k] : 0.0f)); }
        set_length(y, 1.0f);
        /* x = (1, 0, 0) cross y, normalised; z = y cross x. */
        x[0] = 0.0f; x[1] = -y[2]; x[2] = y[1];
        set_length(x, 1.0f);
        z[0] = y[1] * x[2] - y[2] * x[1]; z[1] = y[2] * x[0] - y[0] * x[2]; z[2] = y[0] * x[1] - y[1] * x[0];
        float *m = frame[i];
        memset(m, 0, 64);
        memcpy(m, z, 12); memcpy(m + 4, y, 12); memcpy(m + 8, x, 12); memcpy(m + 12, q, 12); m[15] = 1.0f;
    }
}
