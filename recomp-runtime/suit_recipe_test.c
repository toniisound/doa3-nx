/* suit_recipe tests on synthetic archives (no game data): a copied piece and
   a CPU-skinned record with a transform, a weld (normal included) and trimming, read from a
   folder, from an XDVDFS image and through a pack. */
#include "suit_recipe.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

typedef struct Buf { unsigned char *p; size_t n, cap; } Buf;
static void put(Buf *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 64; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static void u32(Buf *b, unsigned v) { put(b, &v, 4); }
static void f32(Buf *b, float v) { put(b, &v, 4); }
static void loc(Buf *b, unsigned arc, unsigned entry, unsigned off) { unsigned char a[4] = {(unsigned char)arc}; put(b, a, 4); u32(b, entry); u32(b, off); }
static void identity(Buf *b) { for (int i = 0; i < 16; ++i) f32(b, (i % 5) == 0 ? 1.0f : 0.0f); }

/* entry 0: a copied piece (2 vertices, stride 12) and its 3 indices;
   entry 1: a type-0 record (3 vertices: pos, normal, uv) and its indices. */
static Buf archive_bytes(int which)
{
    Buf e = {0};
    if (which == 0) {
        const float v[6] = {1, 2, 3, 4, 5, 6};
        put(&e, v, sizeof v);
        const unsigned short ix[3] = {0, 1, 0};
        put(&e, ix, sizeof ix);
    } else {
        for (int i = 0; i < 3; ++i) {
            const float row[8] = {(float)i, 0, 0, 0, 0, 2, 0.5f, 0.25f};
            put(&e, row, sizeof row);
        }
        const unsigned short ix[3] = {2, 0, 2};
        put(&e, ix, sizeof ix);
    }
    Buf a = {0};
    put(&a, "AFS\0", 4); u32(&a, 1); u32(&a, 16); u32(&a, (unsigned)e.n);
    put(&a, e.p, e.n);
    free(e.p);
    return a;
}

static Buf recipe(void)
{
    Buf r = {0};
    put(&r, "DXR1", 4); u32(&r, 1);
    u32(&r, 4); put(&r, "HEAD", 4);                 /* header bytes are copied through */
    u32(&r, 0);                                     /* textures */
    u32(&r, 2);                                     /* parts */
    /* part 0: copied, XYZ only */
    u32(&r, 12); u32(&r, 0x002); u32(&r, 4); u32(&r, 2); u32(&r, (unsigned)-1); u32(&r, 2); u32(&r, 1);
    u32(&r, 1); identity(&r);
    u32(&r, 0); for (int i = 0; i < 17; ++i) f32(&r, 0); u32(&r, 0); u32(&r, 0); u32(&r, 0);
    u32(&r, 0);                                     /* weights */
    loc(&r, 0, 0, 24); u32(&r, 6);                  /* indices */
    unsigned char k1[4] = {1}; put(&r, k1, 4); loc(&r, 0, 0, 0); u32(&r, 24); u32(&r, 12); u32(&r, 0x002); u32(&r, 0);
    /* part 1: record, moved +10 in x, vertex 0 welded onto copied vertex 1, trimmed to {0, 2} */
    u32(&r, 32); u32(&r, 0x112); u32(&r, 4); u32(&r, 2); u32(&r, (unsigned)-1); u32(&r, 2); u32(&r, 1);
    u32(&r, 0); identity(&r);
    u32(&r, 0); for (int i = 0; i < 17; ++i) f32(&r, 0); u32(&r, 0); u32(&r, 0); u32(&r, 0);
    u32(&r, 0);
    loc(&r, 1, 0, 96); u32(&r, 6);
    unsigned char k2[4] = {2}; put(&r, k2, 4); loc(&r, 1, 0, 0); u32(&r, 0); u32(&r, 3); u32(&r, 1);
    for (int i = 0; i < 16; ++i) f32(&r, (i % 5) == 0 ? 1.0f : (i == 12 ? 10.0f : 0.0f));
    u32(&r, 32); u32(&r, 0x112); u32(&r, 1);
    u32(&r, 0x80000000u); loc(&r, 0, 0, 0); identity(&r); /* weld: vertex 0 -> (1,2,3), normal (4,5,6) */
    u32(&r, 2); u32(&r, 0); u32(&r, 2);              /* own */
    return r;
}

static void write_file(const char *path, const Buf *b)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(b->p, 1, b->n, f); fclose(f); }
}

static void check_image(const unsigned char *img, size_t n)
{
    CHECK(n > 8 && memcmp(img, "HEAD", 4) == 0);
    /* part 0 header, binds, material, state, vertices, indices */
    size_t at = 4 + 32 + 68 + 72 + 12;
    const float *v0 = (const float *)(img + at);
    CHECK(v0[0] == 1.0f && v0[5] == 6.0f);
    at += 24 + 6;
    unsigned h[8]; memcpy(h, img + at, 32);
    CHECK(h[0] == 32 && h[3] == 2 && h[4] == 3);
    at += 32 + 68 + 72 + 12;
    float p[8]; memcpy(p, img + at, 32);
    CHECK(p[0] == 1.0f && p[1] == 2.0f && p[2] == 3.0f);       /* welded */
    CHECK(fabsf(p[3] * p[3] + p[4] * p[4] + p[5] * p[5] - 1.0f) < 1e-5f && fabsf(p[4] / p[3] - 1.25f) < 1e-5f);  /* parent normal, normalized */
    CHECK(p[6] == 0.5f);                                         /* uv kept */
    memcpy(p, img + at + 32, 32);
    CHECK(p[0] == 12.0f && fabsf(p[5] - 1.0f) < 1e-6f);          /* vertex 2 moved +10, own normal normalized */
    unsigned short ix[3]; memcpy(ix, img + at + 64, 6);
    CHECK(ix[0] == 1 && ix[1] == 0 && ix[2] == 1);               /* remapped to the trimmed vertices */
}

static void build_and_check(const char *disc_path, const Buf *r)
{
    RecompSuitDisc *disc = recomp_suit_disc_open(disc_path);
    CHECK(disc != NULL);
    if (!disc) return;
    unsigned char *img = NULL; size_t n = 0;
    CHECK(recomp_suit_recipe_build(r->p, r->n, disc, &img, &n));
    if (img) check_image(img, n);
    free(img);
    unsigned char bad[16] = "DXR1";
    CHECK(!recomp_suit_recipe_build(bad, sizeof bad, disc, &img, &n));
    recomp_suit_disc_close(disc);
}

/* One XYZ part of 2 vertices copied from hdd entry 0 with `stride`, under
   `mirrors` mirror steps. */
static Buf one_part(unsigned stride, unsigned mirrors)
{
    Buf r = {0};
    put(&r, "DXR1", 4); u32(&r, 1); u32(&r, 4); put(&r, "HEAD", 4); u32(&r, 0); u32(&r, 1);
    u32(&r, 12); u32(&r, 0x002); u32(&r, 4); u32(&r, 2); u32(&r, (unsigned)-1); u32(&r, 2); u32(&r, 1);
    u32(&r, 1); identity(&r);
    u32(&r, 0); for (int i = 0; i < 17; ++i) f32(&r, 0); u32(&r, 0); u32(&r, 0); u32(&r, 0);
    u32(&r, 0);
    loc(&r, 0, 0, 24); u32(&r, 6);
    for (unsigned i = 0; i < mirrors; ++i) { unsigned char k3[4] = {3}; put(&r, k3, 4); identity(&r); }
    unsigned char k1[4] = {1}; put(&r, k1, 4); loc(&r, 0, 0, 0); u32(&r, 2 * stride); u32(&r, stride); u32(&r, 0x002); u32(&r, 0);
    return r;
}

static void check_rejected(const char *disc_path)
{
    RecompSuitDisc *disc = recomp_suit_disc_open(disc_path);
    CHECK(disc != NULL);
    if (!disc) return;
    unsigned char *img = NULL; size_t n = 0;
    Buf ok = one_part(12, 2), narrow = one_part(4, 0), deep = one_part(12, 100000);
    CHECK(recomp_suit_recipe_build(ok.p, ok.n, disc, &img, &n));
    free(img); img = NULL;
    CHECK(!recomp_suit_recipe_build(narrow.p, narrow.n, disc, &img, &n));   /* rows narrower than XYZ */
    CHECK(!recomp_suit_recipe_build(deep.p, deep.n, disc, &img, &n));       /* nesting past the limit */
    free(ok.p); free(narrow.p); free(deep.p);
    recomp_suit_disc_close(disc);
}

int main(void)
{
    const char *dir = "suit_recipe_test_disc";
    make_dir(dir);
    Buf hdd = archive_bytes(0), dvd = archive_bytes(1), r = recipe();
    write_file("suit_recipe_test_disc/datahdd.afs", &hdd);
    write_file("suit_recipe_test_disc/datadvd.afs", &dvd);
    build_and_check(dir, &r);
    check_rejected(dir);

    /* The same archives in a minimal XDVDFS image: root directory at sector 33. */
    Buf iso = {0};
    const size_t sector = 2048;
    unsigned char *img = calloc(1, 40 * sector);
    memcpy(img + 32 * sector, "MICROSOFT*XBOX*MEDIA", 20);
    const unsigned root = 33, root_size = 64; memcpy(img + 32 * sector + 20, &root, 4); memcpy(img + 32 * sector + 24, &root_size, 4);
    unsigned char *d = img + 33 * sector;
    /* entry at 0: datahdd.afs, right child at dword 8; entry at 32: datadvd.afs */
    unsigned short right = 8, none = 0; unsigned s1 = 34, s2 = 36, z1 = (unsigned)hdd.n, z2 = (unsigned)dvd.n;
    memcpy(d, &none, 2); memcpy(d + 2, &right, 2); memcpy(d + 4, &s1, 4); memcpy(d + 8, &z1, 4); d[12] = 0x20; d[13] = 11; memcpy(d + 14, "DATAHDD.AFS", 11);
    memcpy(d + 32, &none, 2); memcpy(d + 34, &none, 2); memcpy(d + 36, &s2, 4); memcpy(d + 40, &z2, 4); d[44] = 0x20; d[45] = 11; memcpy(d + 46, "datadvd.afs", 11);
    memcpy(img + 34 * sector, hdd.p, hdd.n); memcpy(img + 36 * sector, dvd.p, dvd.n);
    put(&iso, img, 40 * sector); free(img);
    write_file("suit_recipe_test.iso", &iso);
    build_and_check("suit_recipe_test.iso", &r);

    /* A root directory of 0 or 4 GiB is rejected before it is allocated. */
    const unsigned bad_sizes[] = {0u, 0xFFFFFFFFu};
    for (int i = 0; i < 2; ++i) {
        memcpy(iso.p + 32 * sector + 24, &bad_sizes[i], 4);
        write_file("suit_recipe_test_bad.iso", &iso);
        CHECK(recomp_suit_disc_open("suit_recipe_test_bad.iso") == NULL);
    }

    /* A pack holding the recipe as two chunks. */
    Buf pk = {0};
    put(&pk, "DXP1", 4); u32(&pk, 1); u32(&pk, 1); u32(&pk, 1); u32(&pk, 2); u32(&pk, 2);
    char name[16] = "kasumi"; put(&pk, name, 16); f32(&pk, 6.75f); f32(&pk, 1.75f);
    u32(&pk, 0); u32(&pk, 40); char suit[48] = "Tulip"; put(&pk, suit, 48); u32(&pk, 0); u32(&pk, 2);
    u32(&pk, 0); u32(&pk, 1);
    const unsigned half = (unsigned)(r.n / 2);
    u32(&pk, 0); u32(&pk, half); u32(&pk, half); u32(&pk, (unsigned)r.n - half);
    put(&pk, r.p, r.n);
    write_file("suit_recipe_test.pack", &pk);
    RecompSuitPack *pack = recomp_suit_pack_open("suit_recipe_test.pack");
    CHECK(pack && recomp_suit_pack_suit_count(pack) == 1 && recomp_suit_pack_girl_count(pack) == 1);
    if (pack) {
        CHECK(strcmp(recomp_suit_pack_girl(pack, 0)->name, "kasumi") == 0 && recomp_suit_pack_suit(pack, 0)->suit == 40);
        unsigned char *got = NULL; size_t n = 0;
        CHECK(recomp_suit_pack_recipe(pack, 0, &got, &n) && n == r.n && memcmp(got, r.p, n) == 0);
        free(got);
        CHECK(!recomp_suit_pack_recipe(pack, 1, &got, &n));
        recomp_suit_pack_close(pack);
    }
    free(hdd.p); free(dvd.p); free(r.p); free(iso.p); free(pk.p);
    if (failures) return 1;
    printf("suit recipe: ok\n");
    return 0;
}
