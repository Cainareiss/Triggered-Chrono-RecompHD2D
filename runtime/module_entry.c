/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compiled into every game module: describes the module's tables to the
 * engine that loads it (module.h). */
#define CT_TABLES_DEFINE
#include "module.h"

__attribute__((visibility("default"))) const ct_module_info *ct_module_get(void);

const ct_module_info *ct_module_get(void)
{
    static ct_module_info info;
    info = (ct_module_info){
        CT_MODULE_ABI, sizeof(CPU), sizeof(ct_func), sizeof(ct_overlay_func),
        {ct_funcs, ct_func_count, ct_externs, ct_extern_count, ct_jumptables,
         ct_jumptable_count, ct_overlay_funcs, ct_overlay_func_count},
    };
    return &info;
}
