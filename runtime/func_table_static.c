/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Static builds: install the tables linked into this program (a game's
 * generated ct_funcs.c and ct_overlays.c) before main runs. */
#define CT_TABLES_DEFINE
#include "func_table.h"

__attribute__((constructor)) static void install_linked_tables(void)
{
    const ct_tables t = {ct_funcs, ct_func_count, ct_externs, ct_extern_count,
                         ct_jumptables, ct_jumptable_count, ct_overlay_funcs,
                         ct_overlay_func_count};
    ct_tables_install(&t);
}
