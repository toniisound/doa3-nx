#ifndef DOA3_RECOMP_SUIT_JOINTS_H
#define DOA3_RECOMP_SUIT_JOINTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A DOAXBV joint piece (ankle, elbow, wrist, toes) bent the way DOAXBV bends
   it each frame, on host data. The joint's numbers come from the girl's body
   model on the player's disc; the maths is DOAXBV's own, as readable source
   (body_joints.c, from doaxvb-re's docs/body-joints.md).

   On the disc a girl's body is two archive entries: her geometry (the OBJ
   records, a piece's vertices) and, in the entry after it, her joints. That
   entry's word at +0x10 is the offset of the joint section: a list of
   descriptor offsets (from the section) ending in 0, each descriptor 20
   bytes:
     +0x00 interior list offset   +0x04 frame data offset
     +0x08 mesh (OBJ index in the geometry entry)
     +0x09 bone a  +0x0A bone b  +0x0B flags (0x10 copy list at +0x0C,
           0x20 transform list at +0x10; 0x08 the second ankle variant)
   The interior list holds vertex byte offsets into the mesh (32 bytes a
   vertex); the frame data is 0xA0 bytes (recomp_joint_frames), then one
   32-byte vertex (position, normal at +0x10) per group the list places.
   The game turns bone b 8 into 30 and 14 into 31 for the elbows (the
   upper-arm helpers), as here. No girl body uses copy or transform lists. */

enum { RECOMP_SUIT_JOINT_SKIP = 0xFFFFFFFEu, RECOMP_SUIT_JOINT_END = 0xFFFFFFFFu };

typedef struct RecompSuitJoint {
    uint32_t bone_a, bone_b;      /* the piece is in bone a's space; b is the parent side */
    float frame[40];              /* axes and origins (recomp_joint_frames) */
    uint32_t stream_count;
    float *stream;                /* stream_count x 8 floats: position, -, normal, - */
    uint32_t word_count;
    uint32_t *words;              /* interior list: step, groups of vertex indices
                                     (SKIP: not in this part) each ended by END, an
                                     extra END after a step's groups, 0 at the end */
} RecompSuitJoint;

/* The joint of the mesh whose vertices start at `vbo` in the geometry entry,
   read from the entry bytes; false when that mesh is not a joint piece (or
   uses lists not supported here). */
bool recomp_suit_joint_parse(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t vbo, RecompSuitJoint *out);

/* The vertex offset of the joint mesh on bone a (its first variant, for the
   ankles), from the girl's geometry and joints entries. */
bool recomp_suit_joint_mesh_of(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t bone_a, uint32_t *vbo);

/* Re-indexes a joint for a part holding only some of the mesh's vertices:
   own[i] is the mesh vertex at part index i (count of them). */
bool recomp_suit_joint_select(const RecompSuitJoint *joint, const uint32_t *own, uint32_t own_count, RecompSuitJoint *out);

/* Mirrors a joint's data by a row-vector matrix (a part made from the other
   side's piece): its axes, origins and stream go through m. */
void recomp_suit_joint_mirror(RecompSuitJoint *joint, const float m[16]);

void recomp_suit_joint_free(RecompSuitJoint *joint);

/* Bends a piece: `relative` takes bone b's space into bone a's (bone b's
   world matrix times the inverse of bone a's, row vectors). Positions are at
   +0 of each vertex, normals at normal_offset. Vertices past `count` and
   SKIP targets are left alone. */
void recomp_suit_joint_bend(const RecompSuitJoint *joint, const float relative[16], uint8_t *vertices,
    uint32_t stride, uint32_t normal_offset, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
