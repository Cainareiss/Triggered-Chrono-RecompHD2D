/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The function tables the engine reads (func_table.h). Empty until the
 * linked tables install themselves or a game module is loaded. */
#include "func_table.h"

static const ct_func no_funcs[1];
static const ct_extern no_externs[1];
static const ct_jumptable no_jumptables[1];
static const ct_overlay_func no_overlays[1];

ct_tables ct_tables_cur = {no_funcs, 0, no_externs, 0, no_jumptables, 0, no_overlays, 0};

void ct_tables_install(const ct_tables *t)
{
    ct_tables_cur = *t;
}
