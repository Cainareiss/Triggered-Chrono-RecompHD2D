/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Game modules: a game's generated code built as a shared library on the
 * user's machine from their own ROM, loaded by an engine that ships with
 * no game code at all. The module exports one function, ct_module_get,
 * describing its tables; everything else it calls (bus, scheduler,
 * interpreter) it takes from the engine. */
#ifndef CT_MODULE_H
#define CT_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "cpu.h"
#include "func_table.h"

/* Bump when CPU, the table entry layouts, or what generated code expects
   of the engine change. The ops.h helpers are compiled into the module, so
   a change to what they do counts too: 2 is decimal-mode ADC/SBC (a module
   built for 1 stops on the first ADC/SBC run with D=1); 3 is a compiled
   callee that comes back elsewhere handing the caller's rest to the
   executor (ops.h cpu_return_ok) instead of stopping, and a jump table
   index past the recorded entries read from the bus and dispatched; 4 is
   the scene export (ct_module_info.scene and scene_size, ct_scene). */
#define CT_MODULE_ABI 4

/* ---- the scene: what the game is showing, for renderers (docs/SCENE.md) ----
   A module may describe its current view as a map of cells, tile layers
   and objects, so that a renderer can rebuild it in 3D (frontend/hd2d).
   The module fills a ct_scene from the game's memory once per frame, at
   the frame edge; the engine adds the console's state (VRAM, CGRAM, OAM)
   and never reads game memory itself. Everything is in the console's own
   formats, so nothing here belongs to one game. */
#define CT_SCENE_LAYERS      3
#define CT_SCENE_MAX_CELLS   64                        /* map side, in cells */
#define CT_SCENE_STRIDE      (2 * CT_SCENE_MAX_CELLS)  /* tilemap row, in 8x8 tiles */
#define CT_SCENE_MAX_OBJECTS 64
#define CT_SCENE_MAX_PIECES  16

enum {                       /* ct_scene.mode */
    CT_SCENE_OTHER = 0,      /* not a map view (battle, menu, world map, title...): draw it flat */
    CT_SCENE_FIELD = 1,      /* a map view, as the cells, layers and objects describe it */
};

enum {                       /* ct_scene_cell.cls: what the cell is to walk on */
    CT_CELL_UNKNOWN = 0,
    CT_CELL_FLOOR,           /* walkable */
    CT_CELL_SOLID,           /* blocked */
    CT_CELL_WALLISH,         /* partly blocked (corners, diagonal edges) */
    CT_CELL_WATER,
    CT_CELL_STAIRS,          /* stairs and ramps between levels */
};
enum {                       /* ct_scene_cell.flags */
    CT_CELL_OVERHEAD = 1 << 0,   /* some of its tiles draw over sprites (roofs, canopies) */
    CT_CELL_EVENT    = 1 << 1,   /* an exit, or stepping on it starts an event */
};
typedef struct {
    uint8_t cls;             /* CT_CELL_* class */
    uint8_t flags;           /* CT_CELL_OVERHEAD ... */
    uint8_t plane;           /* walk level the game keeps for it (0: unknown) */
    uint8_t raw;             /* the game's own class value, for debugging */
} ct_scene_cell;

typedef struct {
    uint8_t present;
    uint8_t bpp;             /* bits per pixel of its tiles: 2 or 4 */
    uint16_t chr_base;       /* VRAM word address of tile 0 */
    uint16_t pal_base;       /* CGRAM index of palette 0 */
    /* (2 map_w) x (2 map_h) BG tilemap words, row stride CT_SCENE_STRIDE:
       tile 0-9, palette 10-12, priority 13 (over sprites), h flip 14,
       v flip 15. The module's memory, valid until its next fill. */
    const uint16_t *tiles;
} ct_scene_layer;

typedef struct {
    int32_t x, y;            /* map position of its foot point, px */
    int16_t z;               /* height above its floor, px (0: unknown) */
    uint8_t facing;          /* 0 up, 1 down, 2 left, 3 right, 0xFF unknown */
    uint8_t n_pieces;
    uint8_t piece[CT_SCENE_MAX_PIECES];   /* the OAM entries (0-127) drawing it */
} ct_scene_object;

typedef struct {
    /* Filled by the module. With mode OTHER nothing else need be. */
    uint8_t mode;            /* CT_SCENE_FIELD or CT_SCENE_OTHER */
    uint8_t player;          /* index in object[] of the character the player moves, 0xFF none */
    uint8_t cell_px;         /* cell size in px (16: 2x2 tiles) */
    uint8_t n_objects;
    uint16_t map_w, map_h;   /* in cells, at most CT_SCENE_MAX_CELLS */
    uint32_t map_id;         /* the game's own id for the map */
    uint32_t map_generation; /* changes whenever the map's cells or tiles do */
    int32_t camera_x, camera_y;           /* map px at the screen's top-left */
    ct_scene_layer layer[CT_SCENE_LAYERS];   /* front to back: 0 draws over 1 */
    const ct_scene_cell *cells;           /* map_w x map_h, row stride CT_SCENE_MAX_CELLS */
    ct_scene_object object[CT_SCENE_MAX_OBJECTS];
    /* Filled by the engine (ct_scene_capture): the console's state at the
       frame edge, which the module's description refers to. */
    const uint16_t *vram;    /* 32768 words */
    const uint16_t *cgram;   /* 256 BGR555 colors */
    const uint16_t *oam;     /* 272 words, the console's layout: 2 per entry, then the
                                16 words of size and x bit 8 */
    uint16_t obj_chr[2];     /* VRAM word address of OBJ tiles 0 and 256 */
    uint8_t obj_size[2];     /* small and large OBJ size, px */
    uint8_t brightness;      /* 0-15; 0 also in forced blank */
} ct_scene;

/* Module export (optional): describe this frame's scene. Returns 0 with
   out filled, or -1 when the module can't say. */
typedef int (*ct_module_scene_fn)(ct_scene *out);

typedef struct {
    uint32_t abi;            /* CT_MODULE_ABI the module was built with */
    uint32_t cpu_size;       /* sizeof(CPU) */
    uint32_t func_size;      /* sizeof(ct_func) */
    uint32_t overlay_size;   /* sizeof(ct_overlay_func) */
    ct_tables tables;
    /* The scene export (ABI 4: these two fields and ct_scene's layout). */
    uint32_t scene_size;     /* sizeof(ct_scene) */
    ct_module_scene_fn scene;   /* NULL: no scene description */
} ct_module_info;

typedef const ct_module_info *(*ct_module_get_fn)(void);

/* Load the module at path and install its tables. Call after bus_init and
   before sched_init. Returns 0, or -1 with the reason in *err. */
int ct_module_load(const char *path, const char **err);

/* The scene source: set by ct_module_load from the module, or at startup
   by a program the game is linked into (func_table_static.c). */
void ct_scene_set_source(ct_module_scene_fn fn);
int ct_scene_available(void);
/* This frame's scene: the module's description plus the console's state.
   Call at the frame edge (the frame hook), while the CPU is stopped.
   Returns 0, or -1 when there is none (no source, or the module has none
   to give); out->mode is then CT_SCENE_OTHER. */
int ct_scene_capture(ct_scene *out);

#endif
