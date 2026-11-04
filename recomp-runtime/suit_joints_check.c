/* suit_joints_check <doaxbv disc dir or .iso> <probe.bin> <geometry entry>
   Checks the joint reader and bend against DOAXBV itself: a probe file
   written by a local DOAXBV build at one call of its joint update holds the
   girl's two bone tables, her joint records and each joint mesh's vertices
   after the game bent them. This rebuilds every joint from the disc alone,
   bends the disc's mesh with the probe's matrices and compares every vertex
   the game wrote (position and normal), bit for bit. */
#include "body_joints.h"
#include "suit_joints.h"
#include "suit_recipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TABLE = 2048, BLOB = 4096, VDATA = 16384 };

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s <disc> <probe.bin> <geometry entry>\n", argv[0]); return 2; }
    RecompSuitDisc *disc = recomp_suit_disc_open(argv[1]);
    FILE *f = fopen(argv[2], "rb");
    if (!disc || !f) { fprintf(stderr, "cannot open inputs\n"); return 1; }
    fseek(f, 0, SEEK_END); const long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *probe = malloc((size_t)size);
    if (!probe || fread(probe, 1, (size_t)size, f) != (size_t)size) return 1;
    fclose(f);
    uint32_t head[4]; memcpy(head, probe, 16);
    const float *bone_b_table = (const float *)(probe + 16), *bone_a_table = (const float *)(probe + 16 + TABLE);
    const uint8_t *records = probe + 16 + 3 * TABLE;
    const uint8_t *per_record = records + 1024;
    const size_t record_blob = 4 * BLOB + 4 + VDATA;
    const uint8_t *after = per_record + head[3] * record_blob;

    const uint32_t entry = (uint32_t)atoi(argv[3]);
    uint8_t *geometry, *joints; size_t geometry_size, joints_size;
    if (!recomp_suit_disc_entry(disc, 1, entry, &geometry, &geometry_size) ||
        !recomp_suit_disc_entry(disc, 1, entry + 1, &joints, &joints_size)) { fprintf(stderr, "no entries\n"); return 1; }

    /* Each record is the descriptor in the same place; its mesh's OBJ gives
       the vertices on the disc. */
    uint32_t section; memcpy(&section, joints + 16, 4);
    unsigned failures = 0, compared = 0;
    for (uint32_t r = 0; r < head[3]; ++r) {
        const uint8_t *rec = records + r * 32;
        /* The second ankle variant (skip flag 2) is not bent in the girls'
           usual mode (mask 2); its vertices hold whatever they last had. */
        if (rec[14] & 2u) { printf("record %u: skipped by the game this frame\n", r); continue; }
        uint32_t descriptor; memcpy(&descriptor, joints + section + 4 * r, 4);
        const uint8_t mesh = joints[section + descriptor + 8];
        uint32_t n = 0, vbo = 0, nv = 0;
        for (size_t i = 0; i + 40 <= geometry_size; ++i) {
            if (memcmp(geometry + i, "OBJ", 4) != 0) continue;
            if (n++ == mesh) { memcpy(&nv, geometry + i + 32, 4); memcpy(&vbo, geometry + i + 36, 4); break; }
        }
        RecompSuitJoint joint;
        if (!recomp_suit_joint_parse(geometry, geometry_size, joints, joints_size, vbo, &joint)) {
            printf("record %u: mesh %u not parsed\n", r, mesh); ++failures; continue;
        }
        if (joint.bone_a != rec[12] || joint.bone_b != rec[13]) {
            printf("record %u: bones %u/%u, game %u/%u\n", r, joint.bone_a, joint.bone_b, rec[12], rec[13]); ++failures;
        }
        float relative[16];
        recomp_joint_multiply(relative, bone_b_table + rec[13] * 16, bone_a_table + rec[12] * 16);
        uint8_t *mesh_vertices = malloc((size_t)nv * 32);
        memcpy(mesh_vertices, geometry + vbo, (size_t)nv * 32);
        recomp_suit_joint_bend(&joint, relative, mesh_vertices, 32, 12, nv);
        const uint8_t *game = after + (size_t)r * VDATA;
        unsigned differing = 0, targets = 0;
        /* Compare every vertex: untouched ones must match too. */
        for (uint32_t v = 0; v < nv; ++v) {
            ++targets;
            if (memcmp(mesh_vertices + (size_t)v * 32, game + (size_t)v * 32, 24) != 0) ++differing;
        }
        compared += targets;
        printf("record %u: bones %u<-%u mesh %u, %u vertices, %u differing\n", r, joint.bone_a, joint.bone_b, mesh, nv, differing);
        if (differing) ++failures;
        free(mesh_vertices);
        recomp_suit_joint_free(&joint);
    }
    printf("%s: %u vertices compared, %u records failing\n", argv[2], compared, failures);
    return failures ? 1 : 0;
}
