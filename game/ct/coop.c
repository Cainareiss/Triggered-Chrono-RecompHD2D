/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two-player co-op (docs/SCENE.md, B1): game patches (funcs.toml `patch`),
 * active only while the frontend says two players are on
 * (sched_players). With one player every patch returns at once, without
 * touching memory: the game runs as translated.
 *
 * Battle: party members 2 and 3 take their commands from pad 2. The game
 * reads pad 1 once a frame into $00F2/$00F3 and remaps it with the
 * player's button config into $00F8/$00F9 (Pad_ApplyButtonRemap); the
 * battle turns $00F8 into new presses with auto-repeat
 * (BattleSys_UpdatePadRepeat, $CD0340). While the open command menu
 * ($7E95D5) is member 2's or 3's, the patch at that routine's entry puts
 * pad 2, remapped the same way, in $00F8 instead: the menus, the target
 * cursor and anything else battle code reads from it that frame follow
 * player 2, and player 1's buttons do nothing.
 *
 * At a hand-off (the menu changing owner) the buttons the new owner is
 * already holding are kept from the game until released: otherwise the
 * repeat logic, which compares with the last frame's buttons (the other
 * player's), would see them as new presses. The old owner's buttons just
 * stop, which the game reads as a release. */
#include <stdint.h>

#include "bus.h"
#include "cpu.h"
#include "sched.h"

/* WRAM (bank $7E offsets) */
#define BTL_MENU_SLOT 0x95D5   /* party slot whose command menu is open, $FF none */
#define PAD_HELD      0x00F8   /* remapped held buttons; $F9 the d-pad */
#define PAD_CONFIG    0x2993   /* 8 masks over the raw button byte (Pad_RemapOneSet) */

/* A pad word (sched_set_joypad layout) as the game's remapped $F8/$F9:
   the raw byte A X L R B Y Select Start (as Pad_ReadAndDecode builds
   $F2) through the config masks, bit 0 from $2998 up to bit 7 from $2993
   (Pad_RemapOneSet: eight AND/CMP/ROR steps), and the d-pad as is. */
static uint16_t remap_pad(const uint8_t *w, uint16_t pad)
{
    static const uint16_t order[8] = {0x2998, 0x2996, 0x2997, 0x2994,
                                      0x2999, 0x299A, 0x2995, 0x2993};
    uint8_t raw = (uint8_t)((pad & 0xF0) | (pad >> 12 & 0x0F));
    uint8_t out = 0;
    for (int k = 0; k < 8; k++) {
        uint8_t m = w[order[k]];
        if ((raw & m) == m)
            out |= (uint8_t)(1 << k);
    }
    return (uint16_t)(out | (pad >> 8 & 0x0F) << 8);
}

static int battle_owner;      /* player (0, 1) $F8 came from last time */
static uint16_t battle_mask;  /* held at the hand-off, kept until released */

/* $CD0340 BattleSys_UpdatePadRepeat (m1x0, D=0, DB=$7E). */
int ct_patch_coop_battle_pad(CPU *cpu);
int ct_patch_coop_battle_pad(CPU *cpu)
{
    (void)cpu;
    if (sched_players() < 2)
        return 0;
    uint8_t *w = bus_wram();
    uint8_t slot = w[BTL_MENU_SLOT];
    int owner = slot == 1 || slot == 2;
    uint16_t held = owner ? remap_pad(w, sched_joypad_read(1))
                          : (uint16_t)(w[PAD_HELD] | w[PAD_HELD + 1] << 8);
    if (owner != battle_owner) {
        battle_owner = owner;
        battle_mask = held;
    }
    battle_mask &= held;
    held &= (uint16_t)~battle_mask;
    w[PAD_HELD] = (uint8_t)held;
    w[PAD_HELD + 1] = (uint8_t)(held >> 8);
    return 0;
}

