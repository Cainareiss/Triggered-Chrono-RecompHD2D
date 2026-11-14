/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simulated rumble (runtime/rumble.h, docs/USER-GUIDE.md "Rumble"): game
 * patches (funcs.toml `patch`) and the module's rumble frame hook
 * (ct_game_rumble) that raise rumble events from what the game does. Every
 * patch only reads memory and returns 0, so the game runs as translated;
 * with rumble off they return at once. The interpreter doesn't call
 * patches (--interp-only has battle and event pulses only from the
 * engine's detectors).
 *
 * Found with ct_boot --watch and --log-entry on the gato_win replay
 * (docs/SCENE.md, "Rumble"):
 *
 * - Battle damage. Each battler has a 128-byte record from $7E5E30
 *   (party slots 0-2, then the enemies; Gato's is the seventh, $6130):
 *   current HP +$00, max HP +$02, MP +$04, max MP +$06, status +$1A (bit
 *   7 dead). A hit is queued as {amount, flags} words at $B328 + 4 * slot
 *   (two more lists at $B354 and $B380) and lands in
 *   Battle_ApplyPendingResults ($C1EC7F), which subtracts each amount
 *   from HP (MP when the flags have $4000), clamps to max HP and marks a
 *   battler at 0 dead: Crono's HP $46 -> $3D at frame 10983, Gato's $4C ->
 *   $3B at 11115. The patch at its entry takes a snapshot of every HP and
 *   of the hit's kind; the frame hook compares at the frame edge.
 * - Critical hits: the player's hit roll ($C1ED89) sets $AE9B bit 7 and
 *   doubles the damage (Gato takes $22 instead of $11 at 11729 and 13200).
 * - The action: $B18C is the tech being run; a plain attack is $75 plus
 *   the character (Crono $75, Marle $76), a character's own techs are 1-56
 *   in eights (Crono 1-8, Marle 9-16, ...), and higher ones are double and
 *   triple techs. The party is the characters at $2980-$2982 ($80 none),
 *   in battle slot order.
 * - Earthquakes: event command $F4, EventCmd_ShakeScreen ($C03D84), stores
 *   its operand in direct page $BC: nonzero shakes the field until a
 *   later $F4 00.
 * - The bell: event command $EA, EventCmd_PlaySound ($C03C11), plays sound
 *   $3F for Leene's Bell (the strike heard at frame 3545 of the intro: a
 *   bell's partials at 1171, 1373 and 2058 Hz, ringing out over 2.5 s).
 * - The time gate: Warp_SceneInit ($FDDB97) sets up the gate's tunnel
 *   (the main loop's modes $80-$82, $C03FE4).
 * - Where the game is: the main loop's NMI handler, JML at $0500: $C0EA63
 *   the field, $D1E77C a battle, others menus, the title and the maps
 *   (scene.c uses the same). */
#include <stdint.h>

#include "bus.h"
#include "cpu.h"
#include "rumble.h"
#include "sched.h"

/* WRAM (bank $7E offsets) */
#define NMI_VECTOR   0x0500
#define BATTLER      0x5E30   /* records of 0x80: HP, max HP, ... */
#define BATTLER_SIZE 0x80
#define BATTLER_DEAD 0x1A     /* bit 7 */
#define BATTLERS     11       /* slots 0-2 the party, 3-10 the enemies */
#define RESULTS      0xB328   /* {amount, flags} per slot; also +$2C, +$58 */
#define CRIT_FLAG    0xAE9B   /* bit 7 */
#define ACTION       0xB18C   /* the tech being run */
#define PARTY        0x2980   /* characters in battle slots 0-2, $80 none */
#define EVENT_ARGS   0x12001  /* $7F2001: the event script, indexed by Y */
#define SFX_BELL     0x3F

static const uint8_t nmi_field[4] = {0x5C, 0x63, 0xEA, 0xC0};
static const uint8_t nmi_battle[4] = {0x5C, 0x7C, 0xE7, 0xD1};

static int is(const uint8_t *w, const uint8_t v[4])
{
    return w[NMI_VECTOR] == v[0] && w[NMI_VECTOR + 1] == v[1] && w[NMI_VECTOR + 2] == v[2] &&
           w[NMI_VECTOR + 3] == v[3];
}

static unsigned u16at(const uint8_t *w, unsigned a)
{
    return w[a] | w[a + 1] << 8;
}

/* The player who has battle slot s (0-2). */
static int slot_player(int s)
{
    return sched_slot_player(s);
}

/* The battle slot of the party member who did the action, or -1 (an
   enemy's, or a double or triple tech: everyone's). */
static int actor_slot(const uint8_t *w, unsigned action)
{
    int ch = -1;
    if (action >= 0x75 && action < 0x75 + 7)
        ch = (int)action - 0x75;
    else if (action >= 1 && action <= 56)
        ch = (int)(action - 1) / 8;
    for (int s = 0; ch >= 0 && s < 3; s++)
        if (w[PARTY + s] == ch)
            return s;
    return -1;
}

/* ---- battle damage ---- */

static struct {
    int pending;
    uint16_t hp[BATTLERS];
    uint8_t dead[BATTLERS];
    int crit, tech, actor;
} hit;

/* $C1EC7F Battle_ApplyPendingResults (m1x0): the HP before, once per frame. */
int ct_patch_rumble_results(CPU *cpu);
int ct_patch_rumble_results(CPU *cpu)
{
    (void)cpu;
    if (!rumble_enabled())
        return 0;
    const uint8_t *w = bus_wram();
    if (!hit.pending) {
        for (int s = 0; s < BATTLERS; s++) {
            hit.hp[s] = (uint16_t)u16at(w, BATTLER + s * BATTLER_SIZE);
            hit.dead[s] = w[BATTLER + s * BATTLER_SIZE + BATTLER_DEAD] >> 7;
        }
        hit.crit = hit.tech = 0;
        hit.actor = -1;
        hit.pending = 1;
    }
    unsigned action = w[ACTION];
    hit.crit |= w[CRIT_FLAG] >> 7;
    hit.tech |= action >= 1 && action < 0x75;
    hit.actor = actor_slot(w, action);
    return 0;
}

static uint16_t sat(int v)
{
    return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}

/* At the frame edge after the patch: what changed. */
static void battle_results(const uint8_t *w)
{
    int enemy_hit = 0, enemy_kill = 0;
    for (int s = 0; s < BATTLERS; s++) {
        unsigned rec = BATTLER + s * BATTLER_SIZE;
        int hp = (int)u16at(w, rec), max = (int)u16at(w, rec + 2);
        int dmg = (int)hit.hp[s] - hp;
        if (dmg <= 0 || hit.dead[s] || max <= 0)
            continue;   /* healed, unchanged, or already down */
        int down = hp == 0;
        if (s < 3) {   /* a party member: their player's controller */
            int frac = dmg * 1000 / max;   /* of max HP, in tenths of a percent */
            if (frac > 1000)
                frac = 1000;
            int low = 14000 + frac * 40, high = 8000 + frac * 18, ms = 90 + frac / 4;
            if (hit.crit) {
                low = low * 13 / 10;
                ms += 40;
            }
            if (down) {
                low = 50000;
                high = 22000;
                ms = 380;
            }
            rumble_event_for(slot_player(s), sat(low > 56000 && !down ? 56000 : low), sat(high),
                             (uint32_t)(ms > 300 && !down ? 300 : ms),
                             down ? "ko" : hit.crit ? "hurt-crit" : "hurt");
        } else {
            enemy_hit = 1;
            enemy_kill |= down;
        }
    }
    if (enemy_hit) {   /* the party's hit: the one who did it (everyone's for a combo) */
        int p = hit.actor >= 0 ? slot_player(hit.actor) : RUMBLE_ALL;
        if (hit.tech)
            rumble_event_for(p, 26000, 15000, 200, "tech");
        else if (hit.crit)
            rumble_event_for(p, 22000, 30000, 130, "crit");
        else
            rumble_event_for(p, 9000, 16000, 70, "hit");
        if (enemy_kill)
            rumble_event_for(p, 32000, 12000, 240, "kill");
    }
}

/* ---- field events ---- */

static struct {
    uint8_t on;          /* the operand, while it shakes */
    unsigned at;         /* where it is (direct page $BC) */
    long since;
} quake;

/* $C03D84 EventCmd_ShakeScreen (m1x0): Y at the command byte. */
int ct_patch_rumble_quake(CPU *cpu);
int ct_patch_rumble_quake(CPU *cpu)
{
    if (!rumble_enabled())
        return 0;
    unsigned at = EVENT_ARGS + (cpu->Y & 0xFFFF) + 1;
    if (at >= 0x20000 || cpu->DP + 0xBC >= 0x2000)
        return 0;
    quake.on = bus_wram()[at];
    quake.at = cpu->DP + 0xBC;
    quake.since = -1;
    return 0;
}

/* $C03C11 EventCmd_PlaySound (m1x0): Y at the command byte. */
int ct_patch_rumble_sound(CPU *cpu);
int ct_patch_rumble_sound(CPU *cpu)
{
    if (!rumble_enabled())
        return 0;
    unsigned at = EVENT_ARGS + (cpu->Y & 0xFFFF) + 1;
    if (at < 0x20000 && bus_wram()[at] == SFX_BELL)
        rumble_event_for(RUMBLE_ALL, 30000, 5000, 700, "bell");
    return 0;
}

/* $FDDB97 Warp_SceneInit: into a time gate. */
int ct_patch_rumble_gate(CPU *cpu);
int ct_patch_rumble_gate(CPU *cpu)
{
    (void)cpu;
    if (rumble_enabled())
        rumble_event_for(RUMBLE_ALL, 26000, 18000, 900, "gate");
    return 0;
}

/* ---- the frame hook ---- */

#define QUAKE_REPEAT 6          /* frames between pulses */
#define QUAKE_LIMIT (60 * 12)   /* a long earthquake goes quiet after 12 s */

int ct_game_rumble(long frame);
int ct_game_rumble(long frame)
{
    const uint8_t *w = bus_wram();
    int battle = is(w, nmi_battle), field = is(w, nmi_field);
    if (hit.pending) {
        hit.pending = 0;
        if (battle)
            battle_results(w);
    }
    if (quake.on) {
        if (!field || w[quake.at] != quake.on) {
            quake.on = 0;   /* stopped, or the field is gone */
        } else {
            if (quake.since < 0)
                quake.since = frame;
            long t = frame - quake.since;
            if (t < QUAKE_LIMIT && t % QUAKE_REPEAT == 0)
                rumble_event_for(RUMBLE_ALL, 20000, 6000, 130, "quake");
        }
    }
    return battle || field ? RUMBLE_CTX_PLAY : RUMBLE_CTX_QUIET;
}
