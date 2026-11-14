/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 3D diorama geometry from a scene description (runtime/module.h): the
 * map's tile layers stood up into floors, walls and roofs, and the
 * frame's sprites as billboards. CPU only; hd2d.c draws the result. Every
 * quad is one 8x8 tile of the console's own VRAM, which the renderer's
 * shaders decode as they draw, so nothing here holds pixels.
 *
 * World space: x is the map's x and z its y, in map pixels; y is height
 * above the floor. The game stores no heights, so they are inferred from
 * the cells (docs/HD2D.md, "Heights"): each column's runs of blocked or
 * overhead cells stand up from their south edge, as a wall up to hmax
 * cells high, and whatever is left of a run north of that lies flat on
 * top of it, as a roof. That keeps every tile where the original's
 * three-quarter view drew it (screen row = z - height). */
#ifndef CT_SCENE3D_H
#define CT_SCENE3D_H

#include <stdint.h>

#include "module.h"

/* s3d_vertex.tile: what a quad shows, packed for the shader. */
#define S3D_ADDR_MASK   0x7FFFu      /* VRAM word address of the 8x8 tile */
#define S3D_PAL_SHIFT   15           /* palette, 3 bits */
#define S3D_2BPP        (1u << 18)
#define S3D_OBJ         (1u << 19)   /* sprite palettes (CGRAM 128-255) */
#define S3D_NORMAL_SHIFT 20          /* 3 bits: S3D_N_* */
#define S3D_OPAQUE      (1u << 23)   /* color 0 shows the backdrop instead of nothing */
#define S3D_RAISED      (1u << 24)   /* stood up or lifted (not floor): casts shadows */
#define S3D_OVERHEAD    (1u << 25)   /* the original drew it over sprites */
/* Bits 26-31 are free, for asset packs: a pack's sampling hook (the
   shader's tileTexel(), shaders/scene3d.frag) gets the whole value, so it
   can look a tile up by (VRAM address = character base + tile id * words
   per tile, palette) or by a flag of its own set from s3d_set_cell_hook. */
enum { S3D_N_UP, S3D_N_SOUTH, S3D_N_EAST, S3D_N_WEST, S3D_N_NORTH, S3D_N_SPRITE };

typedef struct {
    float x, y, z;   /* world, map px */
    float u, v;      /* in the tile, 0-8 px, flips applied */
    uint32_t tile;
} s3d_vertex;

typedef struct {
    s3d_vertex *v;
    uint32_t *idx;   /* 6 per quad */
    int nv, ni, cap;   /* cap: quads */
    int ni_floor;    /* indices before this are floor quads (no shadow casters) */
} s3d_mesh;

typedef struct {
    int hmax;        /* wall height cap, cells */
    float stretch;   /* sprite billboards' height per screen px (upright things
                        look shorter from above than the floor does) */
} s3d_params;

void s3d_mesh_free(s3d_mesh *m);

/* The map's geometry: floor quads first (m->ni_floor), then the raised
   ones. Rebuild when the scene's map_generation changes. Returns the
   number of quads, or -1 out of memory. */
int s3d_build_map(const ct_scene *s, const s3d_params *p, s3d_mesh *m);

/* This frame's sprites: one quad per 8x8 piece of every visible OAM entry,
   standing at its object's foot point (entries no object owns stand at
   their own bottom edge). Returns the number of quads. */
int s3d_build_sprites(const ct_scene *s, const s3d_params *p, s3d_mesh *m);

/* One textured quad: corner tl, then a step of 8 px along u (the tile's
   +x) and along v (its +y, downward in the image); tile is the packed
   S3D_* value (VRAM address, palette, normal, flags), hflip and vflip
   mirror the image. For the cell hook below and for the builders. */
void s3d_emit(s3d_mesh *m, const float tl[3], const float u[3], const float v[3],
              uint32_t tile, int hflip, int vflip);

/* Asset packs (later): a hook asked for every cell before the builder
   makes its quads. It may emit its own with s3d_emit and return nonzero,
   and the cell's generated quads are skipped. NULL (the default): none. */
typedef int (*s3d_cell_hook)(void *user, const ct_scene *s, int x, int y, s3d_mesh *m);
void s3d_set_cell_hook(s3d_cell_hook fn, void *user);

/* The part of the map with pixels (map px), from the last s3d_build_map:
   the renderer keeps its camera from showing much beyond it. */
void s3d_map_bounds(int *x0, int *z0, int *x1, int *z1);

/* For tests and the renderer: the inferred height (px) of the top of the
   raised geometry over cell (x, z) of the world grid (16 px cells), from
   the last s3d_build_map. */
int s3d_height_at(int x, int z);

#endif
