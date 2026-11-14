/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compiled into every game module: describes the module's tables to the
 * engine that loads it (module.h). */
#ifndef CT_TABLES_DEFINE   /* also set by the build */
#define CT_TABLES_DEFINE
#endif
#include "module.h"

__attribute__((visibility("default"))) const ct_module_info *ct_module_get(void);

/* The game's scene description, if it has one (a weak reference: NULL
   when the game defines none). */
__attribute__((weak)) int ct_game_scene(ct_scene *out);

const ct_module_info *ct_module_get(void)
{
    static ct_module_info info;
    info = (ct_module_info){
        CT_MODULE_ABI, sizeof(CPU), sizeof(ct_func), sizeof(ct_overlay_func),
        {ct_funcs, ct_func_count, ct_externs, ct_extern_count, ct_jumptables,
         ct_jumptable_count, ct_overlay_funcs, ct_overlay_func_count},
        sizeof(ct_scene), ct_game_scene,
    };
    return &info;
}

/* Poses (module.h ct_module_pose_fn), if the game has them: found by name,
   outside ct_module_info, so engines and modules without them still fit. */
__attribute__((weak)) int ct_game_pose(int obj, ct_scene_pose *out, uint8_t *rgba, int max);
__attribute__((visibility("default"))) int ct_module_pose(int obj, ct_scene_pose *out,
                                                          uint8_t *rgba, int max);

int ct_module_pose(int obj, ct_scene_pose *out, uint8_t *rgba, int max)
{
    return ct_game_pose ? ct_game_pose(obj, out, rgba, max) : -1;
}
