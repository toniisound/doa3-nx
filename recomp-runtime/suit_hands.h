#ifndef DOA3_RECOMP_SUIT_HANDS_H
#define DOA3_RECOMP_SUIT_HANDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A DOAXBV girl's hand gestures, for following DOA3's fists and open hands.

   DOAXBV keeps one mesh per hand and blends it between stored shapes each
   frame (0x00064220, 0x00064FD0), as it does facial expressions. The entry
   after a girl's geometry holds, at the offset in its header word +0x0C, a
   morph section: 'apv\0', a set count, set offsets (from the section), then
   per set {shapes, mesh (bit 31: the shapes are the other hand's, mirrored
   in x), morph vertices, 0, shape table,
   index list, 0}. The table has one {positions, 0} entry per shape (offsets
   from the section; float3 per morph vertex); the index list gives each
   morph vertex's byte offset in the mesh (32 bytes a vertex). Normals are
   not morphed. Every girl's hands keep the same shapes in the same places:
   shape 24 is a closed fist and 14 a flat open hand, the shortest and the
   longest along the hand bone's x axis, which is how they are found here. */

/* For the mesh whose vertices start at `vbo` in the geometry entry: per mesh
   vertex, the offset from its stored position to the fist and to the flat
   hand (3 floats each, malloc'd, *count vertices). False when the mesh has
   no morph set (not a hand). */
bool recomp_suit_hand_shapes(const uint8_t *geometry, size_t geometry_size, const uint8_t *joints, size_t joints_size,
    uint32_t vbo, float **fist, float **flat, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif
