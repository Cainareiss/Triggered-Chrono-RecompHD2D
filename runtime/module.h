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
   executor (ops.h cpu_return_ok) instead of stopping. */
#define CT_MODULE_ABI 3

typedef struct {
    uint32_t abi;            /* CT_MODULE_ABI the module was built with */
    uint32_t cpu_size;       /* sizeof(CPU) */
    uint32_t func_size;      /* sizeof(ct_func) */
    uint32_t overlay_size;   /* sizeof(ct_overlay_func) */
    ct_tables tables;
} ct_module_info;

typedef const ct_module_info *(*ct_module_get_fn)(void);

/* Load the module at path and install its tables. Call after bus_init and
   before sched_init. Returns 0, or -1 with the reason in *err. */
int ct_module_load(const char *path, const char **err);

#endif
