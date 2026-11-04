#ifndef DOA3_RECOMP_SUIT_RECIPE_H
#define DOA3_RECOMP_SUIT_RECIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A DOAXBV swimsuit built for DOA3 from a recipe and the player's own DOAXBV
   disc. The recipe (.dxr) holds only numbers and disc locations: the suit's
   header, materials, bone bindings and masks, and for every piece where its
   bytes are in the disc's archives and the steps that turn them into the
   suit's (transform, welds onto a neighbouring piece, trimming to the used
   vertices, mirroring, re-binding to the chest). No game data ships.

   The result is the same DXC1 image the offline converter writes, which the
   costume injector loads as before. Plain functions: no runtime state. */

#ifdef __cplusplus
extern "C" {
#endif

/* The disc's two archives (datahdd.afs and datadvd.afs) from a directory. */
typedef struct RecompSuitDisc RecompSuitDisc;
RecompSuitDisc *recomp_suit_disc_open(const char *directory);
void recomp_suit_disc_close(RecompSuitDisc *disc);

/* A whole archive entry (arc 0 datahdd, 1 datadvd), malloc'd. */
bool recomp_suit_disc_entry(RecompSuitDisc *disc, uint32_t arc, uint32_t entry, uint8_t **bytes, size_t *size);

/* Builds a DXC1 image from a recipe. Returns false (and frees nothing the
   caller owns) if the recipe is malformed or the disc lacks a range; on
   success *image is malloc'd and owned by the caller. */
bool recomp_suit_recipe_build(const uint8_t *recipe, size_t recipe_size, RecompSuitDisc *disc,
    uint8_t **image, size_t *image_size);

/* The shipped pack of every girl's suit recipes (doaxbv-suits.pack): its
   tables stay in memory, a suit's recipe is read when asked for. */
typedef struct RecompSuitPack RecompSuitPack;
typedef struct RecompSuitPackGirl { char name[16]; float head_up, head_forward; /* cm */ } RecompSuitPackGirl;
typedef struct RecompSuitPackSuit { uint32_t girl, suit; char name[48]; } RecompSuitPackSuit;
RecompSuitPack *recomp_suit_pack_open(const char *path);
void recomp_suit_pack_close(RecompSuitPack *pack);
uint32_t recomp_suit_pack_girl_count(const RecompSuitPack *pack);
const RecompSuitPackGirl *recomp_suit_pack_girl(const RecompSuitPack *pack, uint32_t index);
uint32_t recomp_suit_pack_suit_count(const RecompSuitPack *pack);
const RecompSuitPackSuit *recomp_suit_pack_suit(const RecompSuitPack *pack, uint32_t index);
/* The suit's recipe, malloc'd; false if the pack cannot be read. */
bool recomp_suit_pack_recipe(RecompSuitPack *pack, uint32_t index, uint8_t **recipe, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
