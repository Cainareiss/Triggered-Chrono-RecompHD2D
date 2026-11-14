/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Asset packs: replacement art for the console's tiles and sprites
 * (docs/ASSET-PACKS.md).
 *
 * Everything is keyed by content, from the PPU's provenance G-buffer
 * (sched_prov) and the VRAM/CGRAM/OAM it was drawn from (sched_prov_mem):
 *
 *   BG tile key      64-bit FNV-1a of the 8x8 character's decoded color
 *                    indices (stored orientation, row by row) and the
 *                    palette's colors (15-bit CGRAM values, color 0
 *                    left out: it is transparent), plus the bit depth.
 *   sprite piece key the same for one OAM entry's whole sprite (size x
 *                    size indices, its characters gathered with the
 *                    16x16 page wrap) and its 16-color palette.
 *
 * Flips are not part of a key: a flipped tile is the same tile, and the
 * renderer flips the replacement as the console flips the original.
 * Whole-object keys (a character's animation frame as one picture,
 * rather than the OAM pieces it is drawn with) come later, from a frame
 * decoder for the game's sprite format; until then the dump groups the
 * pieces drawn together into [[object]] entries, as a finding aid.
 *
 * Nothing here is specific to a game: the data comes from the PPU. */
#ifndef CT_PACKS_H
#define CT_PACKS_H

#include <stdint.h>

#include "sched.h"

/* ---- keys ---- */

/* The key of the piece pixel p comes from: its BG tile or its whole
   sprite. 0 if the pixel has nothing to replace (backdrop, mode 7). */
uint64_t packs_key(const sched_vmem *m, const sched_prov_px *p);

/* The piece's original picture as RGBA (color 0 transparent), in stored
   orientation: *w x *h pixels, at most 64 x 64 (rgba holds 64*64*4).
   Returns 0, or -1 if p names nothing. */
int packs_decode(const sched_vmem *m, const sched_prov_px *p, uint8_t *rgba, int *w, int *h);

/* Key as text: 16 lowercase hex digits (buf holds 17). */
void packs_key_text(uint64_t key, char *buf);

/* ---- dumping (--pack-dump) ---- */

/* Start dumping into dir (created if needed; an existing pack.toml there
   is extended: keys already in it are not written again). Returns 0 or
   -1 with the reason printed. */
int packs_dump_open(const char *dir, const char *name);
/* After each frame drawn with provenance: write each piece not seen
   before as a PNG, and each new group of sprites drawn together. */
void packs_dump_frame(long frame, const sched_prov_px *prov, const sched_vmem *m);
/* Counts so far: pieces (tiles and sprites) and objects written. */
void packs_dump_counts(long *pieces, long *objects);
void packs_dump_close(void);

/* ---- loading ---- */

/* Load packs, highest priority first (a key in an earlier pack wins).
   Each is a directory holding pack.toml. Replaces whatever was loaded.
   Returns the number of replacements loaded (0 is not an error), or -1
   if no pack could be read; problems are printed. */
int packs_load(const char *const *dirs, int n);
/* Load the same directories again (F5: after editing PNGs). */
int packs_reload(void);
void packs_unload(void);
int packs_active(void);   /* any replacement loaded */

/* Where a replacement is in the atlas: page (layer of the array texture),
   x, y, w, h in texels; src_w x src_h the original piece's size in
   console pixels (8 x 8 for a BG tile, the sprite's size for a sprite).
   The replacement covers the whole piece in stored orientation. */
typedef struct {
    uint64_t key;
    int page, x, y, w, h;
    int src_w, src_h;
} packs_rect;

/* The replacement for a key, or NULL. This is the lookup a 3D renderer
   (scene3d) calls: key from packs_key, then sample the atlas page in
   rect, flipped as the console flipped the original. */
const packs_rect *packs_lookup_tile(uint64_t key);

/* The atlas: pages x (size x size) RGBA texels, page after page. Valid
   until the next load. Entry i of packs_entries is what the per-pixel
   map's index i + 1 refers to. */
const uint8_t *packs_atlas(int *size, int *pages);
const packs_rect *packs_entries(int *n);
/* Bumped by every load: the atlas changed. */
unsigned packs_generation(void);

/* The per-pixel replacement map for one frame, SCHED_WIDTH x SCHED_HEIGHT
   x 4 uint16: entry index + 1 (0: none), texel u | v << 8 (stored
   orientation), the provenance flags (SCHED_PROV_*), and the 15-bit
   CGRAM color the console drew there (before brightness and color
   math), which the renderer compares with the final pixel to carry fades,
   color math and lighting over to the replacement. Returns the number of
   pixels replaced. */
long packs_map_frame(const sched_prov_px *prov, const sched_vmem *m, uint16_t *map);

/* ---- models (pack.toml [[model]]; the 3D path hooks them up later) ---- */

typedef struct {
    int map;           /* map id */
    int cell_x, cell_y;
    char file[4096];   /* glTF or GLB: the path (pack.toml gives it relative to the pack) */
    float scale;       /* uniform scale (1 if not given) */
    float offset[3];   /* translation in cell units */
    int meshes, primitives;   /* filled by packs_model_parse */
    float min[3], max[3];     /* bounds of the POSITION accessors */
} packs_model;

/* The [[model]] entries of the loaded packs. */
const packs_model *packs_models(int *n);
/* Read and validate path (glTF 2.0, .gltf with its buffers, or .glb):
   counts meshes and primitives and the position bounds into *m. Returns
   0, or -1 with the reason in err (errlen bytes). */
int packs_model_parse(const char *path, packs_model *m, char *err, int errlen);

#endif
