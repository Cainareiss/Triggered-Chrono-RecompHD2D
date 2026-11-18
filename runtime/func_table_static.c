/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Static builds: install the tables linked into this program (a game's
 * generated ct_funcs.c and ct_overlays.c) before main runs, and the game's
 * scene description, rumble hook, music watcher and text if it has them
 * (module.h). */
#ifndef CT_TABLES_DEFINE   /* also set by the build */
#define CT_TABLES_DEFINE
#endif
#include "func_table.h"
#include "module.h"

/* port: PS5 titles (docs/PS5.md): hidden, so a missing one is NULL at
   link time, not an import the console's loader can't resolve. */
#ifdef CT_PS5
#define CT_WEAK_LINKED __attribute__((weak, visibility("hidden")))
#else
#define CT_WEAK_LINKED __attribute__((weak))
#endif
CT_WEAK_LINKED int ct_game_scene(ct_scene *out);   /* NULL: none linked */
CT_WEAK_LINKED int ct_game_rumble(long frame);      /* rumble.h; NULL: none */
CT_WEAK_LINKED extern const ct_music_hooks ct_game_music;
CT_WEAK_LINKED int ct_game_text(ct_text *out);
CT_WEAK_LINKED int ct_game_pose(int obj, ct_scene_pose *out, uint8_t *rgba, int max);

__attribute__((constructor)) static void install_linked_tables(void)
{
    const ct_tables t = {ct_funcs, ct_func_count, ct_externs, ct_extern_count,
                         ct_jumptables, ct_jumptable_count, ct_overlay_funcs,
                         ct_overlay_func_count};
    ct_tables_install(&t);
    ct_scene_set_source(ct_game_scene);
    ct_rumble_set_source(ct_game_rumble);
    ct_music_set_source(&ct_game_music ? &ct_game_music : NULL);
    ct_text_set_source(ct_game_text);
    ct_pose_set_source(ct_game_pose);
}
