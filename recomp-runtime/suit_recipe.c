#include "suit_recipe.h"
#include "suit_joints.h"
#include "suit_hands.h"
#include "suit_legs.h"
#include "suit_skirt.h"
#include "suit_chain.h"

#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the disc's AFS archives -------------------------------------------- */

typedef struct Archive {
    FILE *file;
    uint64_t base;          /* where the archive starts in the file (an ISO image) */
    uint32_t count;
    uint32_t *offset, *size;
} Archive;

struct RecompSuitDisc {
    Archive archive[2]; /* 0 datahdd.afs, 1 datadvd.afs */
};

static bool seek(FILE *f, uint64_t at)
{
#ifdef _WIN32
    return _fseeki64(f, (long long)at, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)at, SEEK_SET) == 0;
#endif
}

static bool read_archive_table(Archive *a)
{
    uint8_t head[8];
    if (!seek(a->file, a->base) || fread(head, 1, 8, a->file) != 8 || memcmp(head, "AFS", 3) != 0) return false;
    memcpy(&a->count, head + 4, 4);
    if (a->count > 1u << 20) return false;      /* DOAXBV's archives hold far fewer entries */
    a->offset = malloc((size_t)a->count * sizeof *a->offset + 1u);
    a->size = malloc((size_t)a->count * sizeof *a->size + 1u);
    if (!a->offset || !a->size) return false;
    for (uint32_t i = 0; i < a->count; ++i) {
        uint32_t e[2];
        if (fread(e, 4, 2, a->file) != 2) return false;
        a->offset[i] = e[0]; a->size[i] = e[1];
    }
    return true;
}

static bool open_archive(Archive *a, const char *directory, const char *name)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    a->file = fopen(path, "rb");
    a->base = 0;
    return a->file && read_archive_table(a);
}

/* An Xbox disc image (XDVDFS): the volume descriptor is sector 32 of the
   game partition, which starts at 0 in a trimmed image and at one of a few
   fixed offsets in a full one. Directory entries form a binary tree:
   u16 left, right (in dwords), u32 sector, u32 size, u8 attributes, u8 name
   length, name. The archives sit in the root directory. */
static const uint64_t xiso_partitions[] = {0, 0x18300000ull, 0xFD90000ull, 0x2080000ull};

static bool xiso_find(FILE *f, uint64_t partition, uint32_t dir_sector, uint32_t dir_size,
    const char *name, uint64_t *at, uint32_t *size)
{
    if (dir_size == 0u || dir_size > 16u << 20) return false;
    uint8_t *dir = malloc((size_t)dir_size + 1u);
    if (!dir || !seek(f, partition + (uint64_t)dir_sector * 2048u) || fread(dir, 1, dir_size, f) != dir_size) { free(dir); return false; }
    uint32_t stack[256], depth = 0; bool found = false;
    stack[depth++] = 0;
    while (depth && !found) {
        const uint32_t o = stack[--depth] * 4u;
        if (o + 14 > dir_size) continue;
        uint16_t left, right; uint32_t sector, length;
        memcpy(&left, dir + o, 2); memcpy(&right, dir + o + 2, 2);
        memcpy(&sector, dir + o + 4, 4); memcpy(&length, dir + o + 8, 4);
        const uint8_t n = dir[o + 13];
        if (left == 0xFFFFu && right == 0xFFFFu) continue;     /* padding */
        if (o + 14 + n <= dir_size && n == strlen(name)) {
            bool same = true;
            for (uint8_t k = 0; k < n && same; ++k) {
                char a = (char)dir[o + 14 + k], b = name[k];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                same = a == b;
            }
            if (same) { *at = partition + (uint64_t)sector * 2048u; *size = length; found = true; }
        }
        if (left && depth < 256) stack[depth++] = left;
        if (right && depth < 256) stack[depth++] = right;
    }
    free(dir);
    return found;
}

static bool open_iso(RecompSuitDisc *d, const char *path)
{
    for (size_t p = 0; p < sizeof xiso_partitions / sizeof xiso_partitions[0]; ++p) {
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        uint8_t vd[28];
        const uint64_t part = xiso_partitions[p];
        if (seek(f, part + 32u * 2048u) && fread(vd, 1, sizeof vd, f) == sizeof vd &&
            memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
            uint32_t root, root_size; memcpy(&root, vd + 20, 4); memcpy(&root_size, vd + 24, 4);
            static const char *const names[2] = {"datahdd.afs", "datadvd.afs"};
            bool ok = true;
            for (int i = 0; i < 2 && ok; ++i) {
                uint32_t size;
                ok = xiso_find(f, part, root, root_size, names[i], &d->archive[i].base, &size);
                if (ok) {
                    d->archive[i].file = i == 0 ? f : fopen(path, "rb");
                    ok = d->archive[i].file && read_archive_table(&d->archive[i]);
                }
            }
            if (ok) return true;
            if (!d->archive[0].file) fclose(f);
            return false;
        }
        fclose(f);
    }
    return false;
}

RecompSuitDisc *recomp_suit_disc_open(const char *directory)
{
    RecompSuitDisc *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    /* A disc image (.iso), or a folder holding the extracted archives. */
    FILE *probe = fopen(directory, "rb");
    bool image = false;
    if (probe) { uint8_t b[4]; image = fread(b, 1, 4, probe) == 4; fclose(probe); }
    if (image ? !open_iso(d, directory) :
        (!open_archive(&d->archive[0], directory, "datahdd.afs") ||
         !open_archive(&d->archive[1], directory, "datadvd.afs"))) {
        recomp_suit_disc_close(d);
        return NULL;
    }
    return d;
}

void recomp_suit_disc_close(RecompSuitDisc *d)
{
    if (!d) return;
    for (int i = 0; i < 2; ++i) {
        if (d->archive[i].file) fclose(d->archive[i].file);
        free(d->archive[i].offset); free(d->archive[i].size);
    }
    free(d);
}

/* Bytes [offset, offset + n) of an archive entry. */
static bool disc_read(RecompSuitDisc *d, uint32_t arc, uint32_t entry, uint32_t offset, void *out, size_t n)
{
    if (arc > 1) return false;
    Archive *a = &d->archive[arc];
    if (entry >= a->count || (uint64_t)offset + n > a->size[entry]) return false;
    if (!seek(a->file, a->base + a->offset[entry] + offset)) return false;
    return fread(out, 1, n, a->file) == n;
}

bool recomp_suit_disc_entry(RecompSuitDisc *d, uint32_t arc, uint32_t entry, uint8_t **bytes, size_t *size)
{
    if (!d || arc > 1 || entry >= d->archive[arc].count) return false;
    const size_t n = d->archive[arc].size[entry];
    uint8_t *p = malloc(n + 1);
    if (!p || !disc_read(d, arc, entry, 0, p, n)) { free(p); return false; }
    *bytes = p; *size = n;
    return true;
}

/* ---- recipe reading and image writing ----------------------------------- */

typedef struct Cursor { const uint8_t *p; size_t n, at; bool ok; } Cursor;

static bool take(Cursor *c, void *out, size_t n)
{
    if (!c->ok || c->at + n > c->n) { c->ok = false; if (out) memset(out, 0, n); return false; }
    if (out) memcpy(out, c->p + c->at, n);
    c->at += n;
    return true;
}
static uint32_t u32(Cursor *c) { uint32_t v = 0; take(c, &v, 4); return v; }

typedef struct Loc { uint32_t arc, entry, offset; } Loc;
static Loc loc(Cursor *c)
{
    uint8_t a[4] = {0}; Loc l;
    take(c, a, 4); l.arc = a[0]; l.entry = u32(c); l.offset = u32(c);
    return l;
}

typedef struct Out { uint8_t *p; size_t n, cap; bool ok; } Out;
static void put(Out *o, const void *data, size_t n)
{
    if (!o->ok) return;
    if (o->n + n > o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 1u << 20;
        while (cap < o->n + n) cap *= 2;
        uint8_t *p = realloc(o->p, cap);
        if (!p) { o->ok = false; return; }
        o->p = p; o->cap = cap;
    }
    memcpy(o->p + o->n, data, n);
    o->n += n;
}

/* ---- vertex layout (mirror of the converter's fvf_layout) --------------- */

typedef struct Layout { uint32_t weights; int normal; uint32_t stride; } Layout;
static Layout layout(uint32_t fvf, uint32_t stride)
{
    Layout l;
    const uint32_t position = fvf & 0x0Eu;
    l.weights = position >= 0x06u ? (position - 4u) / 2u : 0u;
    uint32_t offset = 12u + 4u * l.weights;
    l.normal = (fvf & 0x10u) ? (int)offset : -1;
    l.stride = stride;
    return l;
}

/* A row of `stride` bytes holds everything its FVF places before the rest. */
static bool fits(uint32_t fvf, uint32_t stride)
{
    const Layout l = layout(fvf, stride);
    return stride >= (l.normal >= 0 ? (uint32_t)l.normal + 12u : 12u + 4u * l.weights);
}

/* Row-vector transforms: v' = (v, 1) M, normals v M3. */
static void xform(const float v[3], const float m[16], float out[3], bool point)
{
    float r[3];
    for (int c = 0; c < 3; ++c)
        r[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + (point ? m[12 + c] : 0.0f);
    memcpy(out, r, sizeof r);
}

/* Vertex rows a piece's provenance produces: n rows of `stride` bytes and
   their FVF. */
typedef struct Rows { uint8_t *data; uint32_t n, stride, fvf; } Rows;

static bool provenance(Cursor *c, RecompSuitDisc *d, Rows *r, unsigned depth);

static bool trim_own(Cursor *c, Rows *r, uint32_t **own_out, uint32_t *own_count)
{
    const uint32_t count = u32(c);
    if (!c->ok) return false;
    uint32_t *own = malloc((size_t)count * 4 + 4);
    if (!own) return false;
    take(c, own, (size_t)count * 4);
    if (!c->ok) { free(own); return false; }
    if (count) {
        uint8_t *t = malloc((size_t)count * r->stride);
        if (!t) { free(own); return false; }
        for (uint32_t i = 0; i < count; ++i) {
            if (own[i] >= r->n) { free(t); free(own); return false; }
            memcpy(t + (size_t)i * r->stride, r->data + (size_t)own[i] * r->stride, r->stride);
        }
        free(r->data); r->data = t; r->n = count;
    }
    if (own_out) { *own_out = own; *own_count = count; } else free(own);
    return true;
}

/* The innermost copy/record step's own list (index trimming) is returned
   through these when asked. */
static uint32_t *g_own; static uint32_t g_own_count;
/* The file record a part was built from, and the mirror applied to it, if
   any: a joint piece's numbers are found from them. */
static struct { bool valid; uint32_t arc, entry, vbo, nv, stride; } g_record;
static bool g_mirrored; static float g_mirror[16];

static bool provenance(Cursor *c, RecompSuitDisc *d, Rows *r, unsigned depth)
{
    uint8_t kind[4] = {0};
    take(c, kind, 4);
    if (!c->ok) return false;
    if (kind[0] == 1) {                                   /* copy */
        Loc l = loc(c); uint32_t len = u32(c); r->stride = u32(c); r->fvf = u32(c);
        if (!c->ok || !fits(r->fvf, r->stride)) return false;
        r->n = len / r->stride;
        r->data = malloc((size_t)r->n * r->stride + 1);
        if (!r->data || !disc_read(d, l.arc, l.entry, l.offset, r->data, (size_t)r->n * r->stride)) return false;
        g_record.valid = true; g_record.arc = l.arc; g_record.entry = l.entry; g_record.vbo = l.offset; g_record.nv = r->n;
        g_record.stride = r->stride;
        free(g_own); g_own = NULL; g_own_count = 0;
        return trim_own(c, r, &g_own, &g_own_count);
    }
    if (kind[0] == 2) {                                   /* CPU-skinned file record */
        Loc l = loc(c); uint32_t type = u32(c), nv = u32(c), has_a = u32(c);
        float A[16]; take(c, A, sizeof A);
        r->stride = u32(c); r->fvf = u32(c);
        const uint32_t welds = u32(c);
        if (!c->ok || type > 4 || r->stride < 32 || !fits(r->fvf, r->stride)) return false;
        g_record.valid = true; g_record.arc = l.arc; g_record.entry = l.entry; g_record.vbo = l.offset; g_record.nv = nv;
        g_record.stride = 32u + 4u * type;
        const uint32_t fst = 32u + 4u * type, cw = (r->stride - 32u) / 4u;
        uint8_t *file = malloc((size_t)nv * fst + 1);
        r->n = nv;
        r->data = calloc((size_t)nv, r->stride);
        if (!file || !r->data || !disc_read(d, l.arc, l.entry, l.offset, file, (size_t)nv * fst)) { free(file); return false; }
        if (!has_a) { memset(A, 0, sizeof A); A[0] = A[5] = A[10] = A[15] = 1.0f; }
        for (uint32_t i = 0; i < nv; ++i) {
            const uint8_t *s = file + (size_t)i * fst;
            uint8_t *o = r->data + (size_t)i * r->stride;
            float p[3], n[3], w[4] = {0};
            memcpy(p, s, 12); memcpy(w, s + 12, 4 * type); memcpy(n, s + 12 + 4 * type, 12);
            xform(p, A, p, true); xform(n, A, n, false);
            const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-12f;
            for (int k = 0; k < 3; ++k) n[k] /= len;
            memcpy(o, p, 12);
            for (uint32_t k = 0; k < cw; ++k) { float v = k < type ? w[k] : 0.0f; memcpy(o + 12 + 4 * k, &v, 4); }
            memcpy(o + 12 + 4 * cw, n, 12);
            memcpy(o + 24 + 4 * cw, s + 24 + 4 * type, 8);
        }
        free(file);
        /* Welds: the game moves a joint's parent-side ring onto the parent
           piece; with bit 31 set the parent's normal (after its position)
           comes along too. */
        for (uint32_t i = 0; i < welds; ++i) {
            const uint32_t kw = u32(c), k = kw & 0x7FFFFFFFu; Loc wl = loc(c); float M[16]; take(c, M, sizeof M);
            float p[6];
            const size_t want = (kw & 0x80000000u) ? 24 : 12;
            if (!c->ok || k >= nv || !disc_read(d, wl.arc, wl.entry, wl.offset, p, want)) return false;
            uint8_t *o = r->data + (size_t)k * r->stride;
            xform(p, M, p, true);
            memcpy(o, p, 12);
            if (want == 24) {
                float n[3]; xform(p + 3, M, n, false);
                const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-12f;
                for (int q = 0; q < 3; ++q) n[q] /= len;
                memcpy(o + 12 + 4 * cw, n, 12);
            }
        }
        free(g_own); g_own = NULL; g_own_count = 0;
        return trim_own(c, r, &g_own, &g_own_count);
    }
    if (kind[0] == 3) {                                   /* mirror of another piece */
        float M[16]; take(c, M, sizeof M);
        if (depth >= 8u || !provenance(c, d, r, depth + 1u)) return false;
        g_mirrored = true; memcpy(g_mirror, M, sizeof M);
        const Layout lay = layout(r->fvf, r->stride);
        for (uint32_t i = 0; i < r->n; ++i) {
            uint8_t *o = r->data + (size_t)i * r->stride;
            float p[3]; memcpy(p, o, 12); xform(p, M, p, true); memcpy(o, p, 12);
            if (lay.normal >= 0) { float n[3]; memcpy(n, o + lay.normal, 12); xform(n, M, n, false); memcpy(o + lay.normal, n, 12); }
        }
        return true;
    }
    if (kind[0] == 4) {                                   /* re-bound to the chest */
        const uint32_t src_fvf = u32(c), src_stride = u32(c), nw = u32(c), mats = u32(c);
        if (!c->ok || mats == 0 || mats > 4 || nw > 3) return false;
        float M[4][16];
        for (uint32_t i = 0; i < mats; ++i) take(c, M[i], sizeof M[i]);
        Rows s = {0};
        if (depth >= 8u || !provenance(c, d, &s, depth + 1u) || 12u + 4u * nw > s.stride) { free(s.data); return false; }
        (void)src_fvf; (void)src_stride;
        const Layout lay = layout(s.fvf, s.stride);
        const uint32_t rest = lay.normal >= 0 ? (uint32_t)lay.normal + 12u : 12u + 4u * lay.weights;
        r->fvf = (s.fvf & ~0x0Eu) | 0x02u;
        r->stride = 12u + (lay.normal >= 0 ? 12u : 0u) + (s.stride - rest);
        r->n = s.n;
        r->data = malloc((size_t)r->n * r->stride + 1);
        if (!r->data) { free(s.data); return false; }
        for (uint32_t i = 0; i < s.n; ++i) {
            const uint8_t *v = s.data + (size_t)i * s.stride;
            uint8_t *o = r->data + (size_t)i * r->stride;
            float w[4] = {0}, sum = 0.0f;
            for (uint32_t k = 0; k < nw; ++k) { memcpy(&w[k], v + 12 + 4 * k, 4); sum += w[k]; }
            const uint32_t terms = nw ? nw + 1u : 1u;
            if (nw) w[nw] = 1.0f - sum; else w[0] = 1.0f;
            float p[3], L[3] = {0}, nrm[3] = {0}, n[3];
            memcpy(p, v, 12);
            if (lay.normal >= 0) memcpy(n, v + lay.normal, 12);
            for (uint32_t k = 0; k < terms && k < mats; ++k) {
                float t[3];
                xform(p, M[k], t, true); for (int q = 0; q < 3; ++q) L[q] += w[k] * t[q];
                if (lay.normal >= 0) { xform(n, M[k], t, false); for (int q = 0; q < 3; ++q) nrm[q] += w[k] * t[q]; }
            }
            memcpy(o, L, 12);
            uint32_t at = 12;
            if (lay.normal >= 0) { memcpy(o + at, nrm, 12); at += 12; }
            memcpy(o + at, v + rest, s.stride - rest);
        }
        free(s.data);
        return true;
    }
    return false;
}

/* ---- joint pieces -------------------------------------------------------- */

/* A girl's ankles, elbows, wrists and toes are bent each frame by DOAXBV
   (suit_joints.h). A part re-rigged to bone a and its parent (two binds)
   that came from a body record gets that joint's numbers, read from the
   record's entry and the joints entry after it, so the injector bends it
   the same way instead of blending the two bones. */
typedef struct JointOut { uint32_t part; RecompSuitJoint joint; } JointOut;

static uint32_t mirror_bone(uint32_t b)
{
    static const uint8_t pairs[][2] = {{3, 9}, {4, 10}, {5, 11}, {7, 13}, {8, 14}, {18, 22}, {24, 25}, {30, 31}};
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
        if (b == pairs[i][0]) return pairs[i][1];
        if (b == pairs[i][1]) return pairs[i][0];
    }
    return b;
}

static struct { uint32_t arc, entry; uint8_t *geometry, *joints; size_t geometry_size, joints_size; } g_body;

/* The record's body entries (geometry and the one after), kept for the
   parts that follow from the same body. */
static bool load_body(RecompSuitDisc *d)
{
    if (!g_record.valid || g_record.arc != 1u) return false;
    if (!g_body.geometry || g_body.arc != g_record.arc || g_body.entry != g_record.entry) {
        free(g_body.geometry); free(g_body.joints);
        memset(&g_body, 0, sizeof g_body);
        if (!recomp_suit_disc_entry(d, g_record.arc, g_record.entry, &g_body.geometry, &g_body.geometry_size) ||
            !recomp_suit_disc_entry(d, g_record.arc, g_record.entry + 1u, &g_body.joints, &g_body.joints_size)) {
            free(g_body.geometry); free(g_body.joints);
            memset(&g_body, 0, sizeof g_body);
            return false;
        }
        g_body.arc = g_record.arc; g_body.entry = g_record.entry;
    }
    return true;
}

static bool part_joint(RecompSuitDisc *d, uint32_t bone, RecompSuitJoint *out)
{
    if (!load_body(d)) return false;
    RecompSuitJoint mesh;
    if (!recomp_suit_joint_parse(g_body.geometry, g_body.geometry_size, g_body.joints, g_body.joints_size, g_record.vbo, &mesh))
        return false;
    uint32_t *all = NULL;
    const uint32_t *own = g_own; uint32_t own_count = g_own_count;
    if (!own_count) {                                     /* untrimmed: every vertex */
        all = malloc((size_t)g_record.nv * 4u + 4u);
        if (!all) { recomp_suit_joint_free(&mesh); return false; }
        for (uint32_t i = 0; i < g_record.nv; ++i) all[i] = i;
        own = all; own_count = g_record.nv;
    }
    const bool ok = recomp_suit_joint_select(&mesh, own, own_count, out);
    free(all);
    recomp_suit_joint_free(&mesh);
    if (!ok) return false;
    if (g_mirrored) {
        /* Made from the other side's piece: the same numbers, mirrored, on
           this side's bones. */
        recomp_suit_joint_mirror(out, g_mirror);
        out->bone_a = bone;
        out->bone_b = mirror_bone(out->bone_b);
    } else if (out->bone_a != bone) {
        recomp_suit_joint_free(out);
        return false;
    }
    return true;
}

/* ---- skirts ------------------------------------------------------------- */

/* A cloth piece and the file record it came from, kept so the skirt records
   in that record's script entry (the entry after its geometry) can find the
   piece's vertices. */
typedef struct SkirtSource { uint32_t part, arc, entry, vbo, stride, nv, own_count; uint32_t *own; } SkirtSource;
typedef struct SkirtOut { RecompSuitSkirtShape shape; uint32_t ref_count; RecompSuitSkirtVertex *ref; } SkirtOut;
static uint32_t rd32(const uint8_t *b, size_t size, size_t at) { uint32_t v = 0; if (at + 4u <= size) memcpy(&v, b + at, 4); return v; }
static float rdf(const uint8_t *b, size_t size, size_t at) { float v = 0.0f; if (at + 4u <= size) memcpy(&v, b + at, 4); return v; }

typedef struct ChainOut { RecompSuitChainShape shape; uint32_t ref_count; RecompSuitChainVertex *ref; } ChainOut;

/* The piece vertices a vertex-buffer offset names in a record's pieces. */
static uint32_t source_vertex(const SkirtSource *s, uint32_t word)
{
    const uint32_t original = (word & ~1u) / s->stride;
    if (s->own_count) { for (uint32_t q = 0; q < s->own_count; ++q) if (s->own[q] == original) return q; return UINT32_MAX; }
    return original < s->nv ? original : UINT32_MAX;
}

/* DOAXBV's chain record (0x64 bytes at S + offset, pointers relative to
   S): nodes, bone; floats link length, gravity, inertia, damping, root
   (+0x14), rest force (+0x24), force decay (+0x34), reset follow (+0x38);
   pointers to the colliders (+0x48), the draw list (+0x4C), the node
   point groups (+0x50, each ending at 999.0) and their vertex-buffer
   offsets (+0x54). Only chains that write a suit piece's vertices (draw
   kind 0xFFF9) and capsule colliders (chain shape 3) are read. */
static bool chain_record(const uint8_t *b, size_t size, size_t S, size_t r, RecompSuitChainShape *shape,
    uint32_t *mesh, size_t *points_at, size_t *map_at)
{
    if (r + 0x64u > size) return false;
    const uint32_t nodes = b[r], draw = rd32(b, size, r + 0x4C), groups = rd32(b, size, r + 0x50), map = rd32(b, size, r + 0x54);
    if (!nodes || nodes > RECOMP_SUIT_CHAIN_NODES || !draw || !groups || !map || rd32(b, size, r + 0x60) || S + draw + 4u > size)
        return false;
    const uint32_t kind = (uint32_t)b[S + draw] | (uint32_t)b[S + draw + 1u] << 8;
    if (kind != 0xFFF9u) return false;
    *mesh = ((uint32_t)b[S + draw + 2u] | (uint32_t)b[S + draw + 3u] << 8) & 0xFFFu;
    memset(shape, 0, sizeof *shape);
    shape->nodes = nodes; shape->bone = b[r + 1];
    if (shape->bone >= 32u) return false;
    shape->length = rdf(b, size, r + 4); shape->gravity = rdf(b, size, r + 8); shape->inertia = rdf(b, size, r + 0xC);
    shape->damping = rdf(b, size, r + 0x10); shape->decay = rdf(b, size, r + 0x34); shape->follow = rdf(b, size, r + 0x38);
    for (unsigned k = 0; k < 3u; ++k) { shape->root[k] = rdf(b, size, r + 0x14 + 4u * k); shape->force[k] = rdf(b, size, r + 0x24 + 4u * k); }
    const uint32_t colliders = rd32(b, size, r + 0x48);
    if (colliders)
        for (size_t at = S + colliders; at + 28u <= size && b[at + 1] != 0xFFu && shape->collider_count < RECOMP_SUIT_CHAIN_COLLIDERS; at += 28u) {
            if (b[at + 1] != 3u) continue;
            RecompSuitChainCollider *k = &shape->collider[shape->collider_count++];
            k->bone = b[at]; k->axis = (uint32_t)rdf(b, size, at + 4u);
            k->a = rdf(b, size, at + 8u); k->b = rdf(b, size, at + 12u); k->radius = rdf(b, size, at + 16u);
            k->low = rdf(b, size, at + 20u); k->high = rdf(b, size, at + 24u);
        }
    *points_at = S + groups; *map_at = S + map;
    return true;
}


/* The nth OBJ record's vertex buffer offset in a geometry entry. */
static bool obj_vbo(const uint8_t *g, size_t size, uint32_t index, uint32_t *vbo)
{
    uint32_t seen = 0;
    for (size_t i = 0; i + 40u <= size; ++i) {             /* records need not be aligned */
        if (memcmp(g + i, "OBJ", 4) != 0) continue;
        if (seen++ == index) { *vbo = rd32(g, size, i + 36u); return true; }
    }
    return false;
}

/* DOAXBV's skirt record (0x5C bytes at S + offset; every pointer in it is
   relative to S, the script's secondary-motion section): bytes ring mode,
   axis, bone, -, mesh, cols, rows, split, flags; floats from +0xC default
   length, gravity, inertia, damping, radial, axial, decay, bend, wind, max
   step; then pointers to the row spacings, strand lengths, angles, roots,
   vertex map and colliders. Only the paths DOAXBV's own skirts use are
   read: a skirt along the bone's y axis with its roots in the record. */
static bool skirt_record(const uint8_t *b, size_t size, size_t S, size_t r, RecompSuitSkirtShape *shape,
    uint32_t *mesh, size_t *map_at, size_t *offsets_at)
{
    if (r + 0x5Cu > size) return false;
    const uint32_t axis = b[r + 1], cols = b[r + 5], rows = b[r + 6], flags = b[r + 8];
    if (axis != 1u || (flags & 0x09u) || !cols || !rows || cols > RECOMP_SUIT_SKIRT_MAX || rows > RECOMP_SUIT_SKIRT_MAX) return false;
    memset(shape, 0, sizeof *shape);
    shape->ring_mode = b[r]; shape->bone = b[r + 2]; shape->cols = cols; shape->rows = rows;
    shape->split = b[r + 7] == 0xFFu ? cols / 2u : b[r + 7]; shape->flags = flags;
    *mesh = b[r + 4];
    const float fallback = rdf(b, size, r + 0xC);
    shape->gravity = rdf(b, size, r + 0x10); shape->inertia = rdf(b, size, r + 0x14); shape->damping = rdf(b, size, r + 0x18);
    const float radial = rdf(b, size, r + 0x1C), axial = rdf(b, size, r + 0x20);
    shape->decay = rdf(b, size, r + 0x24); shape->bend = rdf(b, size, r + 0x28); shape->max_step = rdf(b, size, r + 0x30);
    uint32_t ptr[10];
    for (unsigned i = 0; i < 10u; ++i) ptr[i] = rd32(b, size, r + 0x34 + 4u * i);
    /* Flag 0x80 (DOAXBV's writer 0x0005AA30): each grid point places its
       vertices at offsets (bone frame) from a list at +0x58; the heading
       rotation it can apply (+0x54) is used by no skirt on the disc. */
    if (!ptr[3] || !ptr[4] || ptr[8] || ((flags & 0x80u) && !ptr[9])) return false;
    *offsets_at = (flags & 0x80u) ? S + ptr[9] : 0u;
    for (uint32_t i = 0; i < rows; ++i) {
        const float v = ptr[0] ? rdf(b, size, S + ptr[0] + 4u * i) : fallback;
        shape->spacing[i] = v > 90.0f ? fallback : v;
    }
    for (uint32_t i = 0; i < cols; ++i) {
        const float v = ptr[1] ? rdf(b, size, S + ptr[1] + 4u * i) : fallback;
        shape->length[i] = v > 90.0f ? fallback : v;
        for (unsigned k = 0; k < 3u; ++k) shape->root[i][k] = rdf(b, size, S + ptr[3] + 16u * i + 4u * k);
        recomp_suit_skirt_rest_direction(shape->root[i], radial, axial, shape->rest[i]);
    }
    if (shape->root[0][0] > 90.0f) return false;
    if (ptr[5])
        for (size_t at = S + ptr[5]; at + 28u <= size && b[at + 1] != 0xFFu && shape->collider_count < RECOMP_SUIT_SKIRT_COLLIDERS; at += 28u) {
            RecompSuitSkirtCollider *k = &shape->collider[shape->collider_count++];
            k->bone = b[at]; k->type = b[at + 1];
            for (unsigned i = 0; i < 6u; ++i) k->p[i] = rdf(b, size, at + 4u + 4u * i);
        }
    *map_at = S + ptr[4];
    return true;
}

/* Every skirt record in the script entries of the pieces' records, with its
   grid points mapped to the pieces' vertices (part, vertex). */
static uint32_t find_skirts(RecompSuitDisc *d, const SkirtSource *src, uint32_t src_count, SkirtOut **out,
    ChainOut **chains, uint32_t *chain_count)
{
    *out = NULL; *chains = NULL; *chain_count = 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < src_count; ++i) {
        bool seen = false;
        for (uint32_t j = 0; j < i; ++j) seen |= src[j].arc == src[i].arc && src[j].entry == src[i].entry;
        if (seen) continue;
        uint8_t *g = NULL, *b = NULL; size_t gs = 0, bs = 0;
        if (!recomp_suit_disc_entry(d, src[i].arc, src[i].entry, &g, &gs) ||
            !recomp_suit_disc_entry(d, src[i].arc, src[i].entry + 1u, &b, &bs)) { free(g); free(b); continue; }
        const size_t S = rd32(b, bs, 8);
        for (size_t at = S; at + 8u <= bs && b[at] != 0xFFu; at += 8u) {
            if (b[at] == 1u) {
                /* A chain: each node's points, each point's vertex run. */
                ChainOut chain; memset(&chain, 0, sizeof chain);
                uint32_t mesh, vbo; size_t points_at, map_at;
                if (!chain_record(b, bs, S, S + rd32(b, bs, at + 4u), &chain.shape, &mesh, &points_at, &map_at) ||
                    !obj_vbo(g, gs, mesh, &vbo)) continue;
                uint32_t cap = 64, n = 0;
                chain.ref = malloc(cap * sizeof *chain.ref);
                for (uint32_t node = 0; node < chain.shape.nodes && chain.ref; ++node) {
                    for (; points_at + 16u <= bs && rdf(b, bs, points_at) != 999.0f; points_at += 16u) {
                        uint32_t w = rd32(b, bs, map_at); map_at += 4u;
                        if (w == 0xFFFFFFFFu) continue;
                        for (;;) {
                            for (uint32_t k = 0; k < src_count; ++k) {
                                const SkirtSource *s = &src[k];
                                if (s->arc != src[i].arc || s->entry != src[i].entry || s->vbo != vbo || !s->stride) continue;
                                const uint32_t vertex = source_vertex(s, w);
                                if (vertex == UINT32_MAX) continue;
                                if (n == cap) { cap *= 2; RecompSuitChainVertex *grown = realloc(chain.ref, cap * sizeof *chain.ref); if (!grown) break; chain.ref = grown; }
                                chain.ref[n].node = node; chain.ref[n].part = s->part; chain.ref[n].vertex = vertex;
                                for (unsigned q = 0; q < 3u; ++q) chain.ref[n].point[q] = rdf(b, bs, points_at + 4u * q);
                                ++n;
                            }
                            if (!(w & 1u) || map_at >= bs) break;
                            w = rd32(b, bs, map_at); map_at += 4u;
                        }
                    }
                    points_at += 16u;
                }
                if (!chain.ref || !n) { free(chain.ref); continue; }
                chain.ref_count = n;
                ChainOut *grown = realloc(*chains, (*chain_count + 1u) * sizeof **chains);
                if (!grown) { free(chain.ref); continue; }
                *chains = grown; (*chains)[(*chain_count)++] = chain;
                continue;
            }
            if (b[at] != 2u) continue;
            SkirtOut skirt; memset(&skirt, 0, sizeof skirt);
            uint32_t mesh, vbo; size_t map_at, offsets_at;
            if (!skirt_record(b, bs, S, S + rd32(b, bs, at + 4u), &skirt.shape, &mesh, &map_at, &offsets_at) ||
                !obj_vbo(g, gs, mesh & 0x7Fu, &vbo)) continue;
            const uint32_t points = skirt.shape.cols * skirt.shape.rows;
            uint32_t cap = 64, n = 0;
            skirt.ref = malloc(cap * sizeof *skirt.ref);
            /* Each point's vertices: one word chain (bit 0: another vertex
               follows) at the point, or with flag 0x80 one chain per offset
               in the point's list, which ends at 999.0. */
            for (uint32_t p = 0; p < points && skirt.ref; ++p) {
                float offset[3] = {0.0f, 0.0f, 0.0f};
                if (offsets_at) {
                    if (rdf(b, bs, offsets_at) == 999.0f || offsets_at + 16u > bs) { offsets_at += 16u; continue; }
                    for (unsigned k = 0; k < 3u; ++k) offset[k] = rdf(b, bs, offsets_at + 4u * k);
                }
                for (;;) {
                    const uint32_t w = rd32(b, bs, map_at); map_at += 4u;
                    if (w != 0xFFFFFFFFu)
                        for (uint32_t k = 0; k < src_count; ++k) {
                            const SkirtSource *s = &src[k];
                            if (s->arc != src[i].arc || s->entry != src[i].entry || s->vbo != vbo || !s->stride) continue;
                            const uint32_t original = (w & ~1u) / s->stride;
                            uint32_t vertex = UINT32_MAX;
                            if (s->own_count) { for (uint32_t q = 0; q < s->own_count; ++q) if (s->own[q] == original) { vertex = q; break; } }
                            else if (original < s->nv) vertex = original;
                            if (vertex == UINT32_MAX) continue;
                            if (n == cap) { cap *= 2; RecompSuitSkirtVertex *grown = realloc(skirt.ref, cap * sizeof *skirt.ref); if (!grown) break; skirt.ref = grown; }
                            skirt.ref[n].point = p; skirt.ref[n].part = s->part; skirt.ref[n].vertex = vertex;
                            memcpy(skirt.ref[n].offset, offset, sizeof offset); ++n;
                        }
                    if (!(w & 1u) || w == 0xFFFFFFFFu || map_at >= bs) {
                        if (!offsets_at) break;
                        offsets_at += 16u;
                        if (rdf(b, bs, offsets_at) == 999.0f || offsets_at + 16u > bs) { offsets_at += 16u; break; }
                        for (unsigned k = 0; k < 3u; ++k) offset[k] = rdf(b, bs, offsets_at + 4u * k);
                    }
                }
            }
            if (!skirt.ref || !n) { free(skirt.ref); continue; }
            skirt.ref_count = n;
            SkirtOut *grown = realloc(*out, (count + 1u) * sizeof **out);
            if (!grown) { free(skirt.ref); continue; }
            *out = grown; (*out)[count++] = skirt;
        }
        free(g); free(b);
    }
    return count;
}

/* The straight cord behind the ankle (suit_legs.h): per leg, the ankle's
   rest pose (fitted once, from the first body record a leg part comes
   from), and per part on the foot or the shin the vertices the cord can
   move, in the foot's frame at the rest pose. */
typedef struct LegSide { bool fitted; float rest[16]; } LegSide;
typedef struct LegPart { uint32_t part, foot, on_shin, count; float inverse[16]; RecompSuitLegVertex *vertex; } LegPart;

static void affine_invert(const float m[16], float out[16])
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

static void matrix_times(const float a[16], const float b[16], float out[16])
{
    float r[16];
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 4; ++j) {
        double sum = 0; for (unsigned k = 0; k < 4; ++k) sum += (double)a[i*4+k] * b[k*4+j]; r[i*4+j] = (float)sum;
    }
    memcpy(out, r, sizeof r);
}

/* A leg part's cord vertices. `vertices` are the part's own (its first
   bind's space, offset `offset`); an ankle joint's ring is first placed at
   the rest pose, and its targets follow the shin. */
static bool leg_part(const LegSide *side, uint32_t foot, bool on_shin, const float offset[16], const RecompSuitJoint *joint,
    const uint8_t *vertices, uint32_t stride, uint32_t normal_offset, uint32_t count, LegPart *out)
{
    uint8_t *v = malloc((size_t)count * stride + 4u), *ring = calloc(count ? count : 1u, 1);
    out->vertex = calloc(count ? count : 1u, sizeof *out->vertex);
    if (!v || !ring || !out->vertex) { free(v); free(ring); free(out->vertex); return false; }
    memcpy(v, vertices, (size_t)count * stride);
    if (joint) {
        recomp_suit_joint_bend(joint, side->rest, v, stride, normal_offset, count);
        for (uint32_t i = 0; i < joint->word_count && joint->words[i] != 0u; ) {
            ++i;                                                     /* step */
            for (;;) {
                for (; i < joint->word_count && joint->words[i] != RECOMP_SUIT_JOINT_END; ++i)
                    if (joint->words[i] < count) ring[joint->words[i]] = 1;
                ++i;
                if (i < joint->word_count && joint->words[i] == RECOMP_SUIT_JOINT_END) { ++i; break; }
            }
        }
    }
    float to_foot[16];
    if (on_shin) matrix_times(offset, side->rest, to_foot); else memcpy(to_foot, offset, sizeof to_foot);
    out->foot = foot; out->on_shin = on_shin; out->count = 0;
    affine_invert(offset, out->inverse);
    for (uint32_t i = 0; i < count; ++i) {
        float p[3], n[3], pf[3], nf[3];
        memcpy(p, v + (size_t)i * stride, 12);
        memcpy(n, v + (size_t)i * stride + normal_offset, 12);
        for (unsigned c = 0; c < 3u; ++c) {
            pf[c] = p[0] * to_foot[c] + p[1] * to_foot[4 + c] + p[2] * to_foot[8 + c] + to_foot[12 + c];
            nf[c] = n[0] * to_foot[c] + n[1] * to_foot[4 + c] + n[2] * to_foot[8 + c];
        }
        RecompSuitLegVertex *lv = &out->vertex[out->count];
        if (recomp_suit_leg_vertex(side->rest, pf, nf, on_shin || ring[i], lv)) { lv->vertex = i; ++out->count; }
    }
    free(v); free(ring);
    if (!out->count) { free(out->vertex); out->vertex = NULL; return false; }
    return true;
}

/* A hand piece (a part on the hand bone from a body record): per vertex, its
   offsets toward the girl's fist and flat hand (suit_hands.h), which the
   injector blends in as DOA3's own hand closes or opens. */
typedef struct HandOut { uint32_t part, bone, count; float *fist, *flat; } HandOut;

static bool part_hand(RecompSuitDisc *d, uint32_t bone, uint32_t vertices, HandOut *out)
{
    if (g_mirrored || !load_body(d)) return false;
    float *fist, *flat; uint32_t nv;
    if (!recomp_suit_hand_shapes(g_body.geometry, g_body.geometry_size, g_body.joints, g_body.joints_size,
            g_record.vbo, &fist, &flat, &nv)) return false;
    out->bone = bone; out->count = vertices;
    out->fist = calloc(vertices ? vertices : 1u, 12u); out->flat = calloc(vertices ? vertices : 1u, 12u);
    bool ok = out->fist && out->flat;
    for (uint32_t i = 0; i < vertices && ok; ++i) {
        const uint32_t v = g_own_count ? g_own[i] : i;
        ok = v < nv;
        if (ok) { memcpy(out->fist + 3u * i, fist + 3u * v, 12); memcpy(out->flat + 3u * i, flat + 3u * v, 12); }
    }
    free(fist); free(flat);
    if (!ok) { free(out->fist); free(out->flat); }
    return ok;
}

/* ---- the asset ---------------------------------------------------------- */

bool recomp_suit_recipe_build(const uint8_t *recipe, size_t recipe_size, RecompSuitDisc *d,
    uint8_t **image, size_t *image_size)
{
    if (!recipe || !d || !image || !image_size || recipe_size < 12 || memcmp(recipe, "DXR1", 4) != 0) return false;
    Cursor c = {recipe, recipe_size, 4, true};
    Out o = {NULL, 0, 0, true};
    if (u32(&c) != 1u) return false;
    const uint32_t head_len = u32(&c);
    if (!c.ok || c.at + head_len > c.n) return false;
    put(&o, c.p + c.at, head_len); c.at += head_len;
    const uint32_t textures = u32(&c);
    for (uint32_t t = 0; t < textures && c.ok && o.ok; ++t) {
        uint32_t desc[7]; take(&c, desc, sizeof desc);
        Loc l = loc(&c); uint32_t len = u32(&c);
        Loc pl = loc(&c); uint32_t pal_len = u32(&c);
        if (!c.ok) break;
        uint32_t h[9]; memcpy(h, desc, sizeof desc); h[7] = len; h[8] = pal_len;
        put(&o, h, sizeof h);
        uint8_t *buf = malloc((size_t)len + pal_len + 1);
        if (!buf || !disc_read(d, l.arc, l.entry, l.offset, buf, len) ||
            (pal_len && !disc_read(d, pl.arc, pl.entry, pl.offset, buf + len, pal_len))) { free(buf); c.ok = false; break; }
        put(&o, buf, (size_t)len + pal_len);
        free(buf);
    }
    const uint32_t parts = u32(&c);
    JointOut *joints = calloc(parts ? parts : 1, sizeof *joints);
    HandOut *hands = calloc(parts ? parts : 1, sizeof *hands);
    LegPart *legs = calloc(parts ? parts : 1, sizeof *legs);
    LegSide side[2] = {{0}};                        /* feet 3 and 9 */
    SkirtSource *skirt_src = calloc(parts ? parts : 1, sizeof *skirt_src);
    uint32_t skirt_src_count = 0;
    uint32_t joint_count = 0, hand_count = 0, leg_count = 0;
    if (!joints || !hands || !legs) { free(joints); free(hands); free(legs); free(o.p); return false; }
    for (uint32_t pi = 0; pi < parts && c.ok && o.ok; ++pi) {
        g_record.valid = false; g_mirrored = false;
        uint32_t h[7]; take(&c, h, sizeof h);              /* stride fvf primitive count texture flags binds */
        if (!c.ok || h[6] == 0 || h[6] > 4 || !fits(h[1], h[0])) { c.ok = false; break; }
        const size_t binds_at = c.at; take(&c, NULL, (size_t)h[6] * 68);
        const size_t material_at = c.at; take(&c, NULL, 4 + 68 + 12);
        const size_t soft_at = c.at; size_t soft_len = 0;
        if (h[5] & 4u) { soft_len = 64 + (size_t)h[3] * 4; take(&c, NULL, soft_len); }
        const uint32_t weight_count = u32(&c); const size_t weights_at = c.at; take(&c, NULL, (size_t)weight_count * 4);
        Loc il = loc(&c); const uint32_t idx_len = u32(&c);
        Rows r = {0};
        if (!c.ok || !provenance(&c, d, &r, 0u)) { free(r.data); c.ok = false; break; }
        /* A cloth piece straight from a file record: a skirt record may move it. */
        if ((h[5] & 16u) && g_record.valid && !g_mirrored && skirt_src) {
            SkirtSource *s = &skirt_src[skirt_src_count];
            s->part = pi; s->arc = g_record.arc; s->entry = g_record.entry; s->vbo = g_record.vbo;
            s->stride = g_record.stride; s->nv = g_record.nv; s->own_count = g_own_count;
            s->own = g_own_count ? malloc((size_t)g_own_count * 4u) : NULL;
            if (s->own) memcpy(s->own, g_own, (size_t)g_own_count * 4u);
            if (!g_own_count || s->own) ++skirt_src_count;
        }
        /* Compose the final layout: position, the recipe's weights, normal, the rest. */
        const Layout lb = layout(r.fvf, r.stride), lf = layout(h[1], h[0]);
        uint8_t *vtx = r.data; bool composed = false;
        if (r.fvf != h[1] || weight_count) {
            if (r.n != h[3] || (lf.weights && weight_count != (uint64_t)r.n * lf.weights)) { free(r.data); c.ok = false; break; }
            vtx = calloc((size_t)r.n, h[0]); composed = true;
            const uint32_t rest = lb.normal >= 0 ? (uint32_t)lb.normal + 12u : 12u + 4u * lb.weights;
            for (uint32_t i = 0; i < r.n && vtx; ++i) {
                const uint8_t *s = r.data + (size_t)i * r.stride; uint8_t *v = vtx + (size_t)i * h[0];
                uint32_t at = 0;
                memcpy(v, s, 12); at = 12;
                if (lf.weights) { memcpy(v + at, c.p + weights_at + (size_t)i * lf.weights * 4, lf.weights * 4); at += lf.weights * 4; }
                if (lf.normal >= 0) { if (lb.normal >= 0) memcpy(v + at, s + lb.normal, 12); at += 12; }
                if ((uint64_t)at + (r.stride - rest) <= h[0]) memcpy(v + at, s + rest, r.stride - rest);
            }
        }
        if (!vtx || r.n != h[3] || (!composed && r.stride != h[0])) { if (composed) free(vtx); free(r.data); c.ok = false; break; }
        /* A joint piece: bone a carries every vertex (the bend places the
           parent side), so its blend weight becomes 1. */
        uint32_t first_bone; memcpy(&first_bone, c.p + binds_at, 4);
        const bool bent = h[6] == 2u && lf.weights == 1u && part_joint(d, first_bone, &joints[joint_count].joint);
        if (bent) {
            joints[joint_count++].part = pi;
            const float one = 1.0f;
            for (uint32_t i = 0; i < r.n; ++i) memcpy(vtx + (size_t)i * h[0] + 12, &one, 4);
        }
        if (!bent && h[6] == 1u && (first_bone == 5u || first_bone == 11u) && part_hand(d, first_bone, r.n, &hands[hand_count]))
            hands[hand_count++].part = pi;
        /* The cord behind the ankle: foot pieces, the ankle joint and shin
           pieces (single bind, or the ankle joint). */
        {
            const unsigned s_ = first_bone == 3u || first_bone == 7u ? 0u : first_bone == 9u || first_bone == 13u ? 1u : 2u;
            const uint32_t foot = s_ == 0u ? 3u : 9u;
            const bool candidate = s_ < 2u && lf.normal >= 0 && (h[6] == 1u || (bent && first_bone == foot));
            if (candidate && !side[s_].fitted && load_body(d))
                side[s_].fitted = recomp_suit_leg_rest(g_body.geometry, g_body.geometry_size, g_body.joints, g_body.joints_size,
                    foot, side[s_].rest);
            if (candidate && side[s_].fitted) {
                float offset[16]; memcpy(offset, c.p + binds_at + 4, sizeof offset);
                if (leg_part(&side[s_], foot, first_bone != foot, offset, bent ? &joints[joint_count - 1u].joint : NULL,
                        vtx, h[0], (uint32_t)lf.normal, r.n, &legs[leg_count]))
                    legs[leg_count++].part = pi;
            }
        }
        /* Debug: RECOMP_SUIT_JOINTS_DEBUG=1 lists two-bone parts and whether
           they were found to be DOAXBV joint pieces. */
        if (h[6] == 2u && getenv("RECOMP_SUIT_JOINTS_DEBUG"))
            fprintf(stderr, "suit joints: part %u bone %u %s (record %s, entry %u, mirrored %d)\n", pi, first_bone,
                bent ? "bent" : "blended", g_record.valid ? "yes" : "no", g_record.entry, g_mirrored);
        /* Indices from the disc, remapped to the trimmed vertices. */
        uint16_t *idx = malloc((size_t)idx_len + 2);
        if (!idx || !disc_read(d, il.arc, il.entry, il.offset, idx, idx_len)) { free(idx); if (composed) free(vtx); free(r.data); c.ok = false; break; }
        if (g_own && g_own_count) {
            uint32_t top = 0;
            for (uint32_t i = 0; i < g_own_count; ++i) if (g_own[i] > top) top = g_own[i];
            for (uint32_t i = 0; i < idx_len / 2; ++i) if (idx[i] > top) top = idx[i];
            int32_t *remap = calloc((size_t)top + 1, sizeof *remap);
            if (remap) {
                for (uint32_t i = 0; i < g_own_count; ++i) remap[g_own[i]] = (int32_t)i;
                for (uint32_t i = 0; i < idx_len / 2; ++i) idx[i] = (uint16_t)remap[idx[i]];
                free(remap);
            }
        }
        const uint32_t ph[8] = {h[0], h[1], h[2], h[3], idx_len / 2, h[4], h[5], h[6]};
        put(&o, ph, sizeof ph);
        put(&o, c.p + binds_at, (size_t)h[6] * 68);
        put(&o, c.p + material_at, 4 + 68 + 12);
        if (soft_len) put(&o, c.p + soft_at, soft_len);
        put(&o, vtx, (size_t)h[3] * h[0]);
        put(&o, idx, idx_len);
        free(idx); if (composed) free(vtx); free(r.data);
    }
    free(g_own); g_own = NULL; g_own_count = 0;
    SkirtOut *skirts = NULL;
    ChainOut *chains = NULL;
    uint32_t chain_count = 0;
    const uint32_t skirt_count = c.ok && skirt_src_count ? find_skirts(d, skirt_src, skirt_src_count, &skirts, &chains, &chain_count) : 0u;
    for (uint32_t i = 0; i < skirt_src_count; ++i) free(skirt_src[i].own);
    free(skirt_src);
    /* Version 7: the joint pieces' numbers follow the parts, then the hand
       pieces' offsets toward the fist and the flat hand. Version 8 adds the
       skirts after the legs, version 9 the chains after the skirts. */
    if (c.ok && o.ok && (joint_count || hand_count || leg_count || skirt_count || chain_count)) {
        const uint32_t version = chain_count ? 9u : skirt_count ? 8u : 7u;
        if (o.n >= 8) memcpy(o.p + 4, &version, 4);
        put(&o, &joint_count, 4);
        for (uint32_t i = 0; i < joint_count; ++i) {
            const RecompSuitJoint *j = &joints[i].joint;
            const uint32_t h3[3] = {joints[i].part, j->bone_a, j->bone_b};
            put(&o, h3, sizeof h3);
            put(&o, j->frame, sizeof j->frame);
            put(&o, &j->stream_count, 4);
            put(&o, j->stream, (size_t)j->stream_count * 32u);
            put(&o, &j->word_count, 4);
            put(&o, j->words, (size_t)j->word_count * 4u);
        }
        put(&o, &hand_count, 4);
        for (uint32_t i = 0; i < hand_count; ++i) {
            const uint32_t h3[3] = {hands[i].part, hands[i].bone, hands[i].count};
            put(&o, h3, sizeof h3);
            put(&o, hands[i].fist, (size_t)hands[i].count * 12u);
            put(&o, hands[i].flat, (size_t)hands[i].count * 12u);
        }
        /* The legs: each side's rest pose, then the parts' cord vertices. */
        for (unsigned k = 0; k < 2u; ++k) {
            const uint32_t fitted = side[k].fitted;
            put(&o, &fitted, 4); put(&o, side[k].rest, 64);
        }
        put(&o, &leg_count, 4);
        for (uint32_t i = 0; i < leg_count; ++i) {
            const uint32_t h4[4] = {legs[i].part, legs[i].foot, legs[i].on_shin, legs[i].count};
            put(&o, h4, sizeof h4);
            put(&o, legs[i].inverse, 64);
            put(&o, legs[i].vertex, (size_t)legs[i].count * sizeof *legs[i].vertex);
        }
        /* The skirts: each one's numbers (suit_skirt.h), its colliders, and
           its grid points' vertices (point, part, vertex, offset). */
        if (skirt_count || chain_count) {
            put(&o, &skirt_count, 4);
            for (uint32_t i = 0; i < skirt_count; ++i) {
                const RecompSuitSkirtShape *k = &skirts[i].shape;
                const uint32_t h6[6] = {k->bone, k->ring_mode, k->split, k->flags, k->cols, k->rows};
                const float f6[6] = {k->gravity, k->inertia, k->damping, k->decay, k->bend, k->max_step};
                put(&o, h6, sizeof h6); put(&o, f6, sizeof f6);
                put(&o, k->spacing, (size_t)k->rows * 4u);
                put(&o, k->length, (size_t)k->cols * 4u);
                put(&o, k->root, (size_t)k->cols * 12u);
                put(&o, k->rest, (size_t)k->cols * 12u);
                put(&o, &k->collider_count, 4);
                put(&o, k->collider, (size_t)k->collider_count * sizeof k->collider[0]);
                put(&o, &skirts[i].ref_count, 4);
                put(&o, skirts[i].ref, (size_t)skirts[i].ref_count * sizeof *skirts[i].ref);
            }
        }
        /* The chains: each one's numbers (suit_chain.h) and the vertices its
           nodes place (node, part, vertex, point). */
        if (chain_count) {
            put(&o, &chain_count, 4);
            for (uint32_t i = 0; i < chain_count; ++i) {
                put(&o, &chains[i].shape, sizeof chains[i].shape);
                put(&o, &chains[i].ref_count, 4);
                put(&o, chains[i].ref, (size_t)chains[i].ref_count * sizeof *chains[i].ref);
            }
        }
    }
    for (uint32_t i = 0; i < skirt_count; ++i) free(skirts[i].ref);
    free(skirts);
    for (uint32_t i = 0; i < chain_count; ++i) free(chains[i].ref);
    free(chains);
    for (uint32_t i = 0; i < leg_count; ++i) free(legs[i].vertex);
    free(legs);
    for (uint32_t i = 0; i < hand_count; ++i) { free(hands[i].fist); free(hands[i].flat); }
    free(hands);
    for (uint32_t i = 0; i < joint_count; ++i) recomp_suit_joint_free(&joints[i].joint);
    free(joints);
    free(g_body.geometry); free(g_body.joints); memset(&g_body, 0, sizeof g_body);
    if (!c.ok || !o.ok) { free(o.p); return false; }
    *image = o.p; *image_size = o.n;
    return true;
}

/* ---- the shipped pack --------------------------------------------------- */

typedef struct PackSuit { RecompSuitPackSuit info; uint32_t first, count; } PackSuit;

struct RecompSuitPack {
    FILE *file;
    uint32_t girl_count, suit_count, chunk_count, ref_count;
    RecompSuitPackGirl *girls;
    PackSuit *suits;
    uint32_t *refs;
    uint32_t *chunk_offset, *chunk_length;
    uint64_t data;            /* file offset of the chunk data */
};

RecompSuitPack *recomp_suit_pack_open(const char *path)
{
    RecompSuitPack *k = calloc(1, sizeof *k);
    if (!k) return NULL;
    k->file = fopen(path, "rb");
    uint8_t magic[4]; uint32_t head[5];
    bool ok = k->file && fread(magic, 1, 4, k->file) == 4 && memcmp(magic, "DXP1", 4) == 0 &&
        fread(head, 4, 5, k->file) == 5 && head[0] == 1u && head[1] <= 64u && head[2] <= 65536u &&
        head[3] <= (1u << 24) && head[4] <= (1u << 24);
    if (ok) {
        k->girl_count = head[1]; k->suit_count = head[2]; k->chunk_count = head[3]; k->ref_count = head[4];
        k->girls = calloc(k->girl_count + 1u, sizeof *k->girls);
        k->suits = calloc(k->suit_count + 1u, sizeof *k->suits);
        k->refs = malloc(((size_t)k->ref_count + 1u) * 4u);
        k->chunk_offset = malloc(((size_t)k->chunk_count + 1u) * 4u);
        k->chunk_length = malloc(((size_t)k->chunk_count + 1u) * 4u);
        ok = k->girls && k->suits && k->refs && k->chunk_offset && k->chunk_length;
    }
    for (uint32_t i = 0; ok && i < k->girl_count; ++i) {
        ok = fread(k->girls[i].name, 1, 16, k->file) == 16 && fread(&k->girls[i].head_up, 4, 1, k->file) == 1 &&
            fread(&k->girls[i].head_forward, 4, 1, k->file) == 1;
        k->girls[i].name[15] = 0;
    }
    for (uint32_t i = 0; ok && i < k->suit_count; ++i) {
        PackSuit *s = &k->suits[i];
        ok = fread(&s->info.girl, 4, 1, k->file) == 1 && fread(&s->info.suit, 4, 1, k->file) == 1 &&
            fread(s->info.name, 1, 48, k->file) == 48 && fread(&s->first, 4, 1, k->file) == 1 &&
            fread(&s->count, 4, 1, k->file) == 1 && s->info.girl < k->girl_count &&
            (uint64_t)s->first + s->count <= k->ref_count;
        s->info.name[47] = 0;
    }
    ok = ok && fread(k->refs, 4, k->ref_count, k->file) == k->ref_count;
    for (uint32_t i = 0; ok && i < k->ref_count; ++i) ok = k->refs[i] < k->chunk_count;
    for (uint32_t i = 0; ok && i < k->chunk_count; ++i)
        ok = fread(&k->chunk_offset[i], 4, 1, k->file) == 1 && fread(&k->chunk_length[i], 4, 1, k->file) == 1;
    if (ok) {
#ifdef _WIN32
        k->data = (uint64_t)_ftelli64(k->file);
#else
        k->data = (uint64_t)ftello(k->file);
#endif
    }
    if (!ok) { recomp_suit_pack_close(k); return NULL; }
    return k;
}

void recomp_suit_pack_close(RecompSuitPack *k)
{
    if (!k) return;
    if (k->file) fclose(k->file);
    free(k->girls); free(k->suits); free(k->refs); free(k->chunk_offset); free(k->chunk_length);
    free(k);
}

uint32_t recomp_suit_pack_girl_count(const RecompSuitPack *k) { return k ? k->girl_count : 0u; }
const RecompSuitPackGirl *recomp_suit_pack_girl(const RecompSuitPack *k, uint32_t i)
{
    return k && i < k->girl_count ? &k->girls[i] : NULL;
}
uint32_t recomp_suit_pack_suit_count(const RecompSuitPack *k) { return k ? k->suit_count : 0u; }
const RecompSuitPackSuit *recomp_suit_pack_suit(const RecompSuitPack *k, uint32_t i)
{
    return k && i < k->suit_count ? &k->suits[i].info : NULL;
}

bool recomp_suit_pack_recipe(RecompSuitPack *k, uint32_t index, uint8_t **recipe, size_t *size)
{
    if (!k || index >= k->suit_count || !recipe || !size) return false;
    const PackSuit *s = &k->suits[index];
    size_t total = 0;
    for (uint32_t i = 0; i < s->count; ++i) total += k->chunk_length[k->refs[s->first + i]];
    uint8_t *out = malloc(total + 1);
    if (!out) return false;
    size_t at = 0;
    for (uint32_t i = 0; i < s->count; ++i) {
        const uint32_t c = k->refs[s->first + i];
        if (!seek(k->file, k->data + k->chunk_offset[c]) || fread(out + at, 1, k->chunk_length[c], k->file) != k->chunk_length[c]) {
            free(out); return false;
        }
        at += k->chunk_length[c];
    }
    *recipe = out; *size = total;
    return true;
}
