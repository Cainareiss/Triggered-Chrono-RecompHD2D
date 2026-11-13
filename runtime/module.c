/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Game module loader (module.h). */
#include "module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int ct_module_load(const char *path, const char **err)
{
    static char msg[512];
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        snprintf(msg, sizeof msg, "%s", dlerror());
        *err = msg;
        return -1;
    }
    void *sym = dlsym(h, "ct_module_get");
    ct_module_get_fn get;
    memcpy(&get, &sym, sizeof get);   /* object -> function pointer */
    if (!get) {
        snprintf(msg, sizeof msg, "%s: no ct_module_get (not a game module)", path);
        *err = msg;
        dlclose(h);
        return -1;
    }
    const ct_module_info *m = get();
    if (!m || m->abi != CT_MODULE_ABI || m->cpu_size != sizeof(CPU) ||
        m->func_size != sizeof(ct_func) || m->overlay_size != sizeof(ct_overlay_func)) {
        snprintf(msg, sizeof msg, "%s: built for a different engine (module ABI %u, engine %u)",
                 path, m ? m->abi : 0, CT_MODULE_ABI);
        *err = msg;
        dlclose(h);
        return -1;
    }
    ct_tables_install(&m->tables);   /* the module stays loaded for good */
    return 0;
}
