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
 * Whole poses (a character's animation frame as one picture, rather than
 * the OAM pieces it is drawn with) are keyed by the game module's pose
 * key (module.h ct_scene_pose), [[pose]] below. The dump also groups the
 * pieces drawn together into [[object]] entries, as a finding aid.
 *
 * Nothing here is specific to a game: the data comes from the PPU, and
 * pose keys are the module's. */
#ifndef CT_PACKS_H
#define CT_PACKS_H

#include <stdint.h>

#include "sched.h"

/* ---- keys ---- */

/* The key of the piece pixel p comes from: its BG tile or its whole
   sprite. 0 if the pixel has nothing to replace (backdrop, mode 7). */
uint64_t packs_key(const sched_vmem *m, const sched_prov_px *p);

/* The key of the 8x8 BG character at VRAM word address chr, drawn with
   the palette whose color 0 is CGRAM index pal, at bpp 2, 4 or 8 bits:
   the same as packs_key gives a pixel of it. vram is 32768 words, cgram
   256 colors. For renderers that know tiles rather than pixels (the 3D
   path). */
uint64_t packs_key_tile(const uint16_t *vram, const uint16_t *cgram, unsigned chr, int pal, int bpp);

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
int packs_active(void);   /* any replacement or model loaded */

/* Where a replacement is in the atlas: page (layer of the array texture),
   x, y, w, h in texels; src_w x src_h the original piece's size in
   console pixels (8 x 8 for a BG tile, the sprite's size for a sprite).
   The replacement covers the whole piece in stored orientation. */
typedef struct {
    uint64_t key;
    int page, x, y, w, h;
    int src_w, src_h;
    int flags;         /* poses: PACKS_FLIP_H, PACKS_FLIP_V */
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
   orientation), the provenance flags (SCHED_PROV_*, plus 32 where color
   math changed the pixel), and the 15-bit
   CGRAM color the console drew there (before brightness and color
   math), which the renderer compares with the final pixel to carry fades,
   color math and lighting over to the replacement. Returns the number of
   pixels replaced. */
long packs_map_frame(const sched_prov_px *prov, const sched_vmem *m, uint16_t *map);

/* ---- pack.toml lines, for other readers of the same files (music.h) ---- */

typedef struct {
    char table[32];   /* the last header: "tile", "object", "model", "music", "pack" */
    int is_array;     /* [[table]] */
    char key[64];
    char val[1024];   /* a string without its quotes, or the value as written */
} packs_toml_line;
/* Parse one line of pack.toml (s is changed); returns 1 for a header, 2
   for key = value, 0 for a line to skip. */
int packs_toml_parse(char *s, packs_toml_line *t);

/* ---- models (pack.toml [[model]]; the 3D path hooks them up later) ---- */
/* ---- poses (pack.toml [[pose]]): a whole animation frame as one picture ----
   Keyed by the game module's pose key (module.h ct_scene_pose): one
   picture for all the OAM entries an object is drawn with. It wins over
   the entries' own [[tile]] replacements; those, then the original, are
   the fallback. */

/* The replacement for pose key, or NULL. Its flags: PACKS_FLIP_H/V, the
   picture drawn mirrored (a [[pose]] entry's flip). */
const packs_rect *packs_lookup_pose(uint32_t key);
enum { PACKS_FLIP_H = 1, PACKS_FLIP_V = 2 };
/* Any [[pose]] loaded. */
int packs_have_poses(void);

/* One object drawn as a pose this frame, for packs_map_frame: the atlas
   entry (index into packs_entries), the pose's size (console px), whether
   it is drawn mirrored, and the OAM entries the object is drawn with.
   The picture's place on screen is the bounding box of those entries in
   the frame's OAM; where that box isn't w x h (the frame changed, an
   entry hidden), the pose is not used and the entries keep their own
   replacements. */
typedef struct {
    int entry;
    int w, h, hflip, vflip;
    int n;
    uint8_t oam[16];
} packs_pose_place;
/* The poses for the next packs_map_frame (copied; n 0 for none) and the
   OBJ sizes (small, large: px) the frame's OAM is read with. */
void packs_set_poses(const packs_pose_place *p, int n, const uint8_t obj_size[2]);
/* The bounding box (screen px) of OAM entries idx[0..n-1] in the
   console's OAM table (272 words), with the OBJ sizes: 0, or -1 when
   none is visible. */
int packs_oam_box(const uint16_t *oam, const uint8_t obj_size[2], const uint8_t *idx, int n,
                  int *x, int *y, int *w, int *h);

/* ---- models (pack.toml [[model]]): glTF drawn by the 3D view ---- */

enum { PACKS_MODEL_MAX_TILES = 4 };
typedef struct {
    int map;           /* map id; -1: any map (with tiles) */
    int cell_x, cell_y;   /* the footprint, cells: top-left ... */
    int cells_w, cells_h; /* ... and size (cells = [x0, y0, x1, y1], or size with tiles) */
    int tiles_layer, n_tiles;   /* tiles = [layer, TL, TR, BL, BR]: every cell showing these */
    uint16_t tiles[PACKS_MODEL_MAX_TILES];   /* (n_tiles 0: placed by cells) */
    char file[4096];   /* glTF or GLB: the path (pack.toml gives it relative to the pack) */
    float scale[3];    /* map px per model unit (1 if not given; one number: all three) */
    float rotate[3];   /* degrees about x, then y, then z */
    float offset[3];   /* map px, after rotating and scaling: x east, y up, z south */
    float color[4];    /* multiplies the material's base color (1 1 1 1) */
    int linear;        /* filter = "linear" (default nearest) */
    int hide_cells;    /* the generated geometry under the footprint goes */
    int meshes, primitives;   /* filled by packs_model_parse */
    float min[3], max[3];     /* bounds of the POSITION accessors */
} packs_model;

/* The [[model]] entries of the loaded packs. */
const packs_model *packs_models(int *n);
/* Read and validate path (glTF 2.0, .gltf with its buffers, or .glb):
   counts meshes and primitives and the position bounds into *m. Returns
   0, or -1 with the reason in err (errlen bytes). */
int packs_model_parse(const char *path, packs_model *m, char *err, int errlen);

/* A model's triangles in its own units (glTF axes: y up, +z its front),
   every node's transform applied, grouped by material. */
typedef struct {
    float pos[3], nrm[3], uv[2];
    uint8_t rgba[4];   /* COLOR_0, else white */
} packs_mvertex;
typedef struct {
    int first, count;  /* indices */
    float color[4];    /* baseColorFactor */
    int tex;           /* the base color texture, -1 none */
    int cutout;        /* alpha mode MASK or BLEND: drawn where alpha >= 0.5 */
} packs_mpart;
typedef struct {
    uint8_t *rgba;
    int w, h;
} packs_mtex;
typedef struct {
    packs_mvertex *v;
    uint32_t *idx;
    packs_mpart *part;
    packs_mtex *tex;
    int nv, ni, nparts, ntex;
} packs_mesh;

/* Model k's mesh, read on first use and kept until the next load (F5):
   NULL if its file can't be read (said once). Entries naming the same
   file share one. */
const packs_mesh *packs_model_mesh(int k);
/* Build a mesh from a file (packs_model_mesh's loader); free it with
   packs_mesh_free. 0, or -1 with the reason in err. */
int packs_mesh_load(const char *path, packs_mesh *out, char *err, int errlen);
void packs_mesh_free(packs_mesh *m);

#endif
