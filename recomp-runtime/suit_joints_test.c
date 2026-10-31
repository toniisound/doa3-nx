/* suit_joints.c on a made-up body: one OBJ record and a joint section with
   one descriptor, parsed, re-indexed, mirrored and bent. Checked against
   DOAXBV itself by suit_joints_check (needs the disc and a probe). */
#include "suit_joints.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

int main(void)
{
    /* Geometry: an OBJ header whose vertices start at 0x100 (4 vertices). */
    static uint8_t geometry[0x200];
    memcpy(geometry + 0x20, "OBJ", 4);
    put32(geometry + 0x20 + 32, 4); put32(geometry + 0x20 + 36, 0x100);

    /* Joints: section at 0x40, one descriptor at +0x08 (mesh 0, bones 4 and
       8, which the game reads as 30); interior list at +0x20: step 16, one
       group placing a vertex into mesh vertices 1 and 3, then step 0. Frame
       data at +0x40: 0xA0 bytes, then the one stream vertex. */
    static uint8_t joints[0x400];
    put32(joints + 16, 0x40);
    put32(joints + 0x40, 0x08); put32(joints + 0x44, 0);
    uint8_t *d = joints + 0x48;
    put32(d, 0x20); put32(d + 4, 0x40); d[8] = 0; d[9] = 4; d[10] = 8; d[11] = 1;
    uint8_t *l = joints + 0x60;
    const uint32_t list[] = {16u, 32u, 96u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u};
    memcpy(l, list, sizeof list);
    float *frame = (float *)(joints + 0x80);
    for (unsigned i = 0; i < 3; ++i) { frame[i * 8 + i] = 1.0f; frame[i * 8 + 4 + i] = 1.0f; }
    float *stream = (float *)(joints + 0x80 + 0xA0);
    const float vertex[8] = {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    memcpy(stream, vertex, sizeof vertex);

    RecompSuitJoint joint;
    CHECK(recomp_suit_joint_parse(geometry, sizeof geometry, joints, sizeof joints, 0x100, &joint));
    CHECK(joint.bone_a == 4u && joint.bone_b == 30u);
    CHECK(joint.stream_count == 1u);
    CHECK(joint.word_count == 6u && joint.words[1] == 1u && joint.words[2] == 3u);
    RecompSuitJoint none;
    CHECK(!recomp_suit_joint_parse(geometry, sizeof geometry, joints, sizeof joints, 0x120, &none));

    /* A part holding mesh vertices 3 and 0 (in that order). */
    const uint32_t own[] = {3u, 0u};
    RecompSuitJoint part;
    CHECK(recomp_suit_joint_select(&joint, own, 2, &part));
    CHECK(part.words[1] == RECOMP_SUIT_JOINT_SKIP && part.words[2] == 0u);

    /* Step 16 places the stream vertex through `relative` (here a shift). */
    float relative[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 10,0,0,1};
    float vertices[2][8] = {{0}};
    recomp_suit_joint_bend(&part, relative, (uint8_t *)vertices, 32, 12, 2);
    CHECK(vertices[0][0] == 11.0f && vertices[0][1] == 2.0f && vertices[0][2] == 3.0f);
    CHECK(vertices[0][3] == 0.0f && vertices[0][4] == 1.0f && vertices[0][5] == 0.0f);
    CHECK(vertices[1][0] == 0.0f);

    /* Mirroring twice gives the numbers back. */
    const float mirror[16] = {-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    recomp_suit_joint_mirror(&part, mirror);
    CHECK(part.stream[0] == -1.0f && part.frame[0] == -1.0f);
    recomp_suit_joint_mirror(&part, mirror);
    CHECK(memcmp(part.stream, vertex, sizeof vertex) == 0);

    recomp_suit_joint_free(&part);
    recomp_suit_joint_free(&joint);
    if (!failures) printf("suit joints: ok\n");
    return failures ? 1 : 0;
}
