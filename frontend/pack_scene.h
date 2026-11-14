/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: asset packs in the scene (docs/ASSET-PACKS.md): what packs.h
 * loaded, put where the game module's scene (module.h) says.
 *
 *   models   [[model]] entries matched to the map (by map id and cells,
 *            or by the tiles a cell shows), placed and baked into world
 *            space for the 3D view (hd2d_pack_models), and the cells
 *            they hide (s3d_set_cell_hook).
 *   poses    each object's pose (ct_scene_pose_of) looked up in the
 *            packs' [[pose]] entries: one picture for all its OAM
 *            entries, in the 2D map (packs_set_poses) and as the 3D
 *            billboard (s3d_set_sprite_hook).
 *   sheets   --pack-export-sheets: every pose seen, per character, laid
 *            out on sheets for artists, with an index; and the maps'
 *            cells (their tile words), for placing models.
 *
 * Nothing here knows a game: keys, sheets and tile words are the
 * module's. */
#ifndef CT_PACK_SCENE_H
#define CT_PACK_SCENE_H

#include <stdint.h>

#include "hd2d/hd2d.h"
#include "module.h"

/* Hook models and poses up to the renderer (hd2d_pack_models, the
   scene3d hooks). Call after every packs_load / packs_reload. */
void pack_scene_attach(void);

/* hd2d_model_fn: the loaded [[model]]s on sc's map. */
int pack_scene_models(void *user, const ct_scene *sc, hd2d_models *out);

/* Whether poses need the scene each frame (pose replacements loaded, or
   the sheet export on). */
int pack_scene_wants_poses(void);
/* At every frame edge with this frame's scene (NULL: none): the objects'
   poses, for the 3D view now and the 2D map of the next frame; the
   sheet export's new poses and maps. */
void pack_scene_frame(const ct_scene *sc);
/* Before packs_map_frame: the poses for the frame just drawn. */
void pack_scene_map_poses(void);

/* --pack-export-sheets: start collecting into dir (created; what an
   earlier export there collected is kept); scale > 1 also writes each
   sheet that many times larger. 0 or -1. */
int pack_scene_export_open(const char *dir, int scale);
/* Write the sheets and indexes (at exit). Returns the number of poses. */
int pack_scene_export_close(void);

#endif
