/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Static builds: install the tables linked into this program (a game's
 * generated ct_funcs.c and ct_overlays.c) before main runs, and the game's
 * scene description if it has one (module.h). */
#ifndef CT_TABLES_DEFINE   /* also set by the build */
#define CT_TABLES_DEFINE
#endif
#include "func_table.h"
#include "module.h"

__attribute__((weak)) int ct_game_scene(ct_scene *out);   /* NULL: none linked */

__attribute__((constructor)) static void install_linked_tables(void)
{
    const ct_tables t = {ct_funcs, ct_func_count, ct_externs, ct_extern_count,
                         ct_jumptables, ct_jumptable_count, ct_overlay_funcs,
                         ct_overlay_func_count};
    ct_tables_install(&t);
    ct_scene_set_source(ct_game_scene);
}
