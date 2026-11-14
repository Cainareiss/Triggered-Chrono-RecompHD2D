/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Always run (frontend QOL, settings.ini always_run; sched.h
 * SCHED_PREF_ALWAYS_RUN): game patches (funcs.toml `patch`) that invert the
 * run button in the field. Off (the default), every patch returns 0 at
 * once without touching anything: the game runs as translated.
 *
 * How the game decides (docs/SCENE.md, A0.8): each frame the party is the
 * player's ($0138 = 0), Player_ApplyPadMoveIntent ($C088EE) dispatches on
 * the d-pad ($00F9 & $0F, JSR ($8902,X)) to one of eight direction
 * routines, $C08925 ... $C08A3C. Each tests the remapped run bit ($00F8 bit
 * 1, the button the player's config gives "dash", B by default: $2996 in
 * Pad_RemapOneSet's masks) and sets the leader's velocity, direct page
 * $2C (X) and $2D (Y) with D = $1D00, to +-$20 running or +-$10 walking,
 * and adds the same to the camera's accumulators $2E / $30. The walk/run
 * animation (Player_SelectMoveAnim: |velocity| >= $18 runs) and the
 * followers' longer lag when running follow from that velocity.
 *
 * Each patch does its routine's work with the run bit inverted, exactly
 * as the routine would with the other bit: the same stores, A and flags
 * left by its last ADC/SBC, then its RTS. The run bit itself is left as
 * the pad set it, so events that test it ($C066B3 and friends) and menus
 * see the real button. The interpreter doesn't call patches
 * (--interp-only has no always-run). */
#include <stdint.h>

#include "alu.h"
#include "bus.h"
#include "cpu.h"
#include "ops.h"
#include "sched.h"

#define PAD_HELD 0x00F8   /* remapped held buttons */
#define RUN_BIT  0x02

/* The direction routine for (dx, dy), each -1, 0 or +1. */
static int move(CPU *cpu, int dx, int dy)
{
    if (!sched_pref(SCHED_PREF_ALWAYS_RUN))
        return 0;
    uint8_t *w = bus_wram();
    unsigned dp = cpu->DP;
    if (dp + 0x30 >= 0x2000)
        return 0;   /* not the field's direct page (never seen): as translated */
    int run = !(w[PAD_HELD] & RUN_BIT);   /* inverted */
    uint8_t s = run ? 0x20 : 0x10;
    /* WRAM directly (bank 0's low 8 KB), as coop.c does: no bus accesses
       inside the instruction accounting of the call. */
    if (dx)
        w[dp + 0x2C] = (uint8_t)(dx > 0 ? s : -s);
    if (dy)
        w[dp + 0x2D] = (uint8_t)(dy > 0 ? s : -s);
    uint8_t a = 0;
    for (int axis = 0; axis < 2; axis++) {
        int d = axis ? dy : dx;
        unsigned at = dp + (axis ? 0x30 : 0x2E);
        if (!d)
            continue;
        cpu->c = d < 0;   /* CLC before ADC, SEC before SBC */
        a = d > 0 ? alu_adc8(cpu, w[at], s) : alu_sbc8(cpu, w[at], s);
        w[at] = a;
    }
    set_a8(cpu, a);
    op_rts(cpu);
    return 1;
}

/* funcs.toml patch names: the eight direction routines. */
int ct_patch_always_run_e(CPU *cpu);
int ct_patch_always_run_w(CPU *cpu);
int ct_patch_always_run_n(CPU *cpu);
int ct_patch_always_run_s(CPU *cpu);
int ct_patch_always_run_se(CPU *cpu);
int ct_patch_always_run_sw(CPU *cpu);
int ct_patch_always_run_ne(CPU *cpu);
int ct_patch_always_run_nw(CPU *cpu);
int ct_patch_always_run_e(CPU *cpu) { return move(cpu, 1, 0); }
int ct_patch_always_run_w(CPU *cpu) { return move(cpu, -1, 0); }
int ct_patch_always_run_n(CPU *cpu) { return move(cpu, 0, -1); }
int ct_patch_always_run_s(CPU *cpu) { return move(cpu, 0, 1); }
int ct_patch_always_run_se(CPU *cpu) { return move(cpu, 1, 1); }
int ct_patch_always_run_sw(CPU *cpu) { return move(cpu, -1, 1); }
int ct_patch_always_run_ne(CPU *cpu) { return move(cpu, 1, -1); }
int ct_patch_always_run_nw(CPU *cpu) { return move(cpu, -1, -1); }
