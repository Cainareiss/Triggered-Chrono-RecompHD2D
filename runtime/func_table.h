/* Tables the translator emits for one game (ct_funcs.c): recompiled
 * functions, extern call boundaries, and (abs,X) jump table bounds. The
 * interpreter and test harness read them through these declarations;
 * func_table_empty.c provides empty ones for builds with no game. */
#ifndef CT_FUNC_TABLE_H
#define CT_FUNC_TABLE_H

#include <stdint.h>

#include "cpu.h"

typedef struct {
    const char *name;
    uint32_t addr;
    uint8_t m, x;
    uint16_t size;
    int db, dp;         /* entry DB/DP from funcs.toml, -1 if unknown */
    uint8_t calls_extern; /* takes an extern hook itself (never dispatched natively) */
    void (*fn)(CPU *);
} ct_func;

extern const ct_func ct_funcs[];
extern const unsigned ct_func_count;

/* call boundaries: target -> runtime hook */
typedef struct {
    uint32_t addr;
    int kind;           /* 0 JSR, 1 JSL, 2 JML */
    void (*hook)(CPU *);
} ct_extern;

extern const ct_extern ct_externs[];
extern const unsigned ct_extern_count;

/* (abs,X) jump table bounds from funcs.toml */
typedef struct {
    uint32_t site;
    uint16_t count;
} ct_jumptable;

extern const ct_jumptable ct_jumptables[];
extern const unsigned ct_jumptable_count;

/* Code the game puts in WRAM, recompiled from the image it was loaded from
 * (overlays, #92; ct_overlays.c). One entry per function and entry state:
 * it runs native only while WRAM $lo-$hi (the bytes it was decoded from)
 * still hashes to `hash` (FNV-1a 64); otherwise it is interpreted. */
typedef struct {
    const char *name;
    uint32_t addr;      /* entry, 24-bit WRAM address */
    uint8_t m, x;
    uint32_t lo, hi;    /* bytes decoded from, inclusive */
    uint64_t hash;
    void (*fn)(CPU *);
    const uint8_t *image;   /* the whole blob it came from (tests load it) */
    uint32_t base, size;    /* where the blob sits in WRAM */
} ct_overlay_func;

extern const ct_overlay_func ct_overlay_funcs[];
extern const unsigned ct_overlay_func_count;

/* port: the engine reads the tables through ct_tables_cur, which the
   linked tables install at startup (static builds: func_table_static.c,
   func_table_empty.c) or a loaded game module replaces (module.h). Code
   that reads the tables uses the names above unchanged; files that define
   them compile with CT_TABLES_DEFINE so the names stay the arrays. */
typedef struct {
    const ct_func *funcs;
    unsigned func_count;
    const ct_extern *externs;
    unsigned extern_count;
    const ct_jumptable *jumptables;
    unsigned jumptable_count;
    const ct_overlay_func *overlay_funcs;
    unsigned overlay_func_count;
} ct_tables;

extern ct_tables ct_tables_cur;
void ct_tables_install(const ct_tables *t);

#ifndef CT_TABLES_DEFINE
#define ct_funcs (ct_tables_cur.funcs)
#define ct_func_count (ct_tables_cur.func_count)
#define ct_externs (ct_tables_cur.externs)
#define ct_extern_count (ct_tables_cur.extern_count)
#define ct_jumptables (ct_tables_cur.jumptables)
#define ct_jumptable_count (ct_tables_cur.jumptable_count)
#define ct_overlay_funcs (ct_tables_cur.overlay_funcs)
#define ct_overlay_func_count (ct_tables_cur.overlay_func_count)
#endif

#endif
