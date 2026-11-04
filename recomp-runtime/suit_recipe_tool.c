/* suit_recipe_tool <doaxbv disc dir or .iso> <recipe.dxr> <out.dxc>
   suit_recipe_tool <doaxbv disc dir or .iso> <suits.pack> <suit index> <out.dxc>
   Builds a suit image from a recipe and the disc (for checking against the
   converter); with a pack, from that suit's recipe in it. */
#include "suit_recipe.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) { fprintf(stderr, "usage: %s <disc> <recipe.dxr> <out.dxc> | <disc> <pack> <index> <out.dxc>\n", argv[0]); return 2; }
    RecompSuitDisc *disc = recomp_suit_disc_open(argv[1]);
    if (!disc) { fprintf(stderr, "cannot open the archives in %s\n", argv[1]); return 1; }
    unsigned char *r = NULL; long n = 0; FILE *f;
    if (argc == 5) {
        RecompSuitPack *pack = recomp_suit_pack_open(argv[2]);
        size_t size = 0;
        if (!pack || !recomp_suit_pack_recipe(pack, (uint32_t)atoi(argv[3]), &r, &size)) { fprintf(stderr, "pack read failed\n"); return 1; }
        const RecompSuitPackSuit *s = recomp_suit_pack_suit(pack, (uint32_t)atoi(argv[3]));
        printf("%s suit %u %s\n", recomp_suit_pack_girl(pack, s->girl)->name, s->suit, s->name);
        n = (long)size;
        recomp_suit_pack_close(pack);
    } else {
        f = fopen(argv[2], "rb");
        if (!f) return 1;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        r = malloc((size_t)n);
        if (!r || fread(r, 1, (size_t)n, f) != (size_t)n) return 1;
        fclose(f);
    }
    unsigned char *image; size_t size;
    if (!recomp_suit_recipe_build(r, (size_t)n, disc, &image, &size)) { fprintf(stderr, "build failed\n"); return 1; }
    const char *out = argv[argc - 1];
    f = fopen(out, "wb");
    if (!f || fwrite(image, 1, size, f) != size) return 1;
    fclose(f);
    printf("%s %zu bytes\n", out, size);
    recomp_suit_disc_close(disc);
    return 0;
}
