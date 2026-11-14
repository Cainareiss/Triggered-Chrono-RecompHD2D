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
 * stop, which the game reads as a release.
 *
 * Field (B2): player 2 steers follower 1 (the party member in slot 2).
 * The leader appends each of its steps to a 128-entry ring at $7F0C00
 * (Obj_RecordMovement, head $01AB) and follower 1 replays it 16-24 steps
 * behind (Field_MoveFollower1, $C0A26B, index $01AC; follower 2 replays
 * behind that, $01AD). While pad 2 pushes the d-pad, the patch at that
 * routine's entry moves follower 1 itself: a step of 1 px (2 px with B
 * held) a frame, checked against the map's solidity grid, with facing and
 * walk animation set as the routine would for that step. It keeps $01AC
 * moving as the routine would have (so follower 2 still follows the
 * leader) and adds up the steps it skipped: that sum is where follower 1
 * would be now, its slot. When pad 2 lets go, follower 1 walks back to its
 * slot (after a short pause) and the routine takes over again, exactly
 * where it would have been. It snaps to the slot instead when it can't
 * get there, when it is more than a screen from the leader, and as soon as
 * an event takes the party ($0138, checked at EventCmd_ActAsPartyMember,
 * $C03002) or the routine misses a frame (a map change, a battle, a
 * cutscene that moves the party itself). The leader keeps every trigger:
 * exits, touch tiles, talking and battles only look at the leader. */
#include <stdint.h>

#include "bus.h"
#include "cpu.h"
#include "ops.h"
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

/* ---- field: follower 1 ---- */

/* WRAM (bank $7E offsets; field code runs with D = $0100) */
#define F_EVENT       0x0138   /* nonzero: an event holds the party (ActAsPartyMember returns) */
#define F_IDLE_TIMER  0x0158   /* 16-bit, cleared by every step (Player_SelectMoveAnim) */
#define F_CUR_OBJ     0x016D   /* object the field engine is running */
#define F_LEADER      0x0197   /* party slots: object index (2 x slot) */
#define F_FOLLOWER1   0x0199
#define F_RING_HEAD   0x01AB   /* the leader's last ring entry */
#define F_RING_F1     0x01AC   /* follower 1's */
#define F_LEADER_DX   0x1D32   /* the leader's step this frame (1/16 px), 0 standing */
#define F_LEADER_DY   0x1D33
#define F_WRAP_X      0x1D1E   /* map grid masks */
#define F_WRAP_Y      0x1D1F
#define OBJ_FACING    0x1600   /* 0 up 1 down 2 left 3 right */
#define OBJ_ANIM_TIME 0x1601
#define OBJ_ANIM      0x1680   /* 0 stand, 1 walk, 6 run */
#define OBJ_ANIM_STEP 0x1681
#define OBJ_X         0x1800   /* 1/16 px, 16-bit: high byte = 16 px tile */
#define OBJ_Y         0x1880
#define OBJ_VX        0x1900   /* this frame's step, signed 1/16 px */
#define OBJ_VY        0x1980
#define OBJ_PRIO_TOP  0x0C00
#define OBJ_PRIO_BOT  0x0C01
#define MAP_PROP0     0x30C0   /* + y*256 + x: bits 2-7 solidity class */
#define MAP_PROP_A    0x7000   /* bit 6: sprite priority, top half */
#define MAP_PROP_B    0x7040   /* bit 6: bottom half; bit 7: blocks NPCs */
#define RING_DX       0x10C00  /* $7F0C00: the ring (7 tables of 128) */
#define RING_DY       0x10C80

#define SCREEN_W    256
#define SCREEN_H    224
#define RETURN_WAIT 20    /* frames pad 2 rests before follower 1 walks back */
#define STUCK_SNAP  45    /* frames without getting closer before it snaps */

enum { F_FOLLOW, F_STEER, F_RETURN };
static struct {
    int state;
    long frame;            /* frame it last ran in (the field's loop isn't
                              tied to frame edges: 0-2 runs a frame) */
    int x, y;              /* follower 1 after this frame's step, 1/16 px */
    int sx, sy;            /* its slot: where the ring would have it */
    int idle, stuck, best; /* frames pad 2 rested, without progress; best distance */
} f1;

static int s8(uint8_t v) { return v < 0x80 ? v : v - 0x100; }
static int rd16(const uint8_t *w, uint32_t a) { return w[a] | w[a + 1] << 8; }

static void wr16(uint8_t *w, uint32_t a, int v)
{
    w[a] = (uint8_t)v;
    w[a + 1] = (uint8_t)(v >> 8);
}

/* The map cell under a point (1/16 px): solid for player 2's follower?
   Solidity class 0 and the special classes ($16 and up: stairs, ramps)
   are walkable; 1 (solid), the shaped classes 2-$15 (partly solid
   corners, taken as solid) and NPC-blocking cells are not. The z-plane
   bits are not looked at. */
static int cell_solid(const uint8_t *w, int x16, int y16)
{
    if (x16 < 0 || y16 < 0)
        return 1;
    unsigned tx = (unsigned)(x16 >> 8) & w[F_WRAP_X], ty = (unsigned)(y16 >> 8) & w[F_WRAP_Y];
    unsigned off = ty * 256 + tx, cls = w[MAP_PROP0 + off] >> 2;
    return (cls >= 1 && cls < 0x16) || (w[MAP_PROP_B + off] & 0x80);
}

/* A 10 x 6 px box at the feet (the foot point at its bottom middle). */
static int box_solid(const uint8_t *w, int x16, int y16)
{
    return cell_solid(w, x16 - 5 * 16, y16) || cell_solid(w, x16 + 5 * 16, y16) ||
           cell_solid(w, x16 - 5 * 16, y16 - 5 * 16) || cell_solid(w, x16 + 5 * 16, y16 - 5 * 16);
}

/* A step, each axis on its own so it slides along walls: what's left of
   (dx, dy) after the map. */
static void try_step(const uint8_t *w, int x, int y, int *dx, int *dy)
{
    if (*dx && box_solid(w, x + *dx, y))
        *dx = 0;
    if (*dy && box_solid(w, x + *dx, y + *dy))
        *dy = 0;
}

/* Facing and walk/run animation for a step, as Player_SelectMoveAnim's
   table ($C09FF7) and its tails ($C0A171 walk, $C0A18C run, $C0A211
   stand) set them for a follower: a straight step faces its way; a
   diagonal keeps the facing if it is one of its two directions, else
   faces the horizontal one. A change of animation restarts it. */
static void set_move_anim(uint8_t *w, unsigned obj, int dx, int dy)
{
    int anim = 0;
    if (dx || dy) {
        int hf = dx < 0 ? 2 : 3, vf = dy < 0 ? 0 : 1, face = w[OBJ_FACING + obj];
        if (!dy)
            face = hf;
        else if (!dx)
            face = vf;
        else if (face != hf && face != vf)
            face = hf;
        w[OBJ_FACING + obj] = (uint8_t)face;
        w[F_IDLE_TIMER] = w[F_IDLE_TIMER + 1] = 0;
        anim = dx > 16 || dx < -16 || dy > 16 || dy < -16 ? 6 : 1;
    }
    if (w[OBJ_ANIM + obj] != anim) {
        w[OBJ_ANIM + obj] = (uint8_t)anim;
        w[OBJ_ANIM_STEP + obj] = 0;
        w[OBJ_ANIM_TIME + obj] = 0;
    }
}

/* Follower 1's ring index, moved as Field_MoveFollower1 would move it this
   frame; the step it would have taken goes to the slot. */
static void advance_ring(uint8_t *w)
{
    int ab = w[F_RING_HEAD], ac = w[F_RING_F1];
    int need = (w[F_LEADER_DX] | w[F_LEADER_DY]) ? 0x18 : 0x10;
    if (((ab - ac) & 0x7F) < need)
        return;
    ac = (ac + 1) & 0x7F;
    w[F_RING_F1] = (uint8_t)ac;
    f1.sx += s8(w[RING_DX + ac]);
    f1.sy += s8(w[RING_DY + ac]);
}

/* Back into the follow ring: onto its slot, the routine in charge again. */
static void snap(uint8_t *w, unsigned obj)
{
    if (f1.state != F_FOLLOW) {
        wr16(w, OBJ_X + obj, f1.sx);
        wr16(w, OBJ_Y + obj, f1.sy);
    }
    f1.state = F_FOLLOW;
}

/* $C03002 EventCmd_ActAsPartyMember (m1x0): runs for each party member
   every frame, events or not. An event holding the party snaps follower 1
   back as it starts. Always runs the routine. */
int ct_patch_coop_party_member(CPU *cpu);
int ct_patch_coop_party_member(CPU *cpu)
{
    (void)cpu;
    if (f1.state == F_FOLLOW || sched_players() < 2)
        return 0;
    uint8_t *w = bus_wram();
    unsigned obj = w[F_FOLLOWER1];
    if (w[F_EVENT] && obj < 0x80 && w[F_CUR_OBJ] == obj)
        snap(w, obj);
    return 0;
}

/* $C0A26B Field_MoveFollower1 (m1x1, D=$0100; JSR from $C03037, Y and
   $016D the follower): with player 2 steering, the whole routine. */
int ct_patch_coop_follower(CPU *cpu);
int ct_patch_coop_follower(CPU *cpu)
{
    if (sched_players() < 2) {
        f1.state = F_FOLLOW;
        return 0;
    }
    uint8_t *w = bus_wram();
    unsigned obj = w[F_FOLLOWER1], lead = w[F_LEADER];
    long now = sched_frame_count();
    if (obj >= 0x80 || lead >= 0x80) {
        f1.state = F_FOLLOW;
        return 0;
    }
    int x = rd16(w, OBJ_X + obj), y = rd16(w, OBJ_Y + obj);
    if (f1.state != F_FOLLOW && (now - f1.frame > 3 || x != f1.x || y != f1.y)) {
        snap(w, obj);   /* it stopped running for a while, or something else moved it */
        x = f1.sx;
        y = f1.sy;
    }
    f1.frame = now;
    uint16_t p2 = sched_joypad_read(1);
    int px = (p2 & 0x0100 ? 1 : 0) - (p2 & 0x0200 ? 1 : 0);   /* right, left */
    int py = (p2 & 0x0400 ? 1 : 0) - (p2 & 0x0800 ? 1 : 0);   /* down, up */
    int speed = p2 & 0x8000 ? 32 : 16;                         /* B runs */
    if (f1.state == F_FOLLOW) {
        if (!px && !py)
            return 0;   /* the routine follows as usual */
        f1.state = F_STEER;
        f1.sx = x;
        f1.sy = y;
    }
    advance_ring(w);
    int dx = 0, dy = 0;
    if (px || py) {
        f1.state = F_STEER;
        f1.idle = 0;
        dx = px * speed;
        dy = py * speed;
        try_step(w, x, y, &dx, &dy);
    } else if (f1.state == F_STEER && ++f1.idle >= RETURN_WAIT) {
        f1.state = F_RETURN;
        f1.stuck = 0;
        f1.best = 1 << 30;
    }
    if (f1.state == F_RETURN) {
        int ex = f1.sx - x, ey = f1.sy - y;
        int dist = (ex < 0 ? -ex : ex) + (ey < 0 ? -ey : ey);
        if (dist < f1.best) {
            f1.best = dist;
            f1.stuck = 0;
        }
        if (dist <= 32 || ++f1.stuck >= STUCK_SNAP) {
            snap(w, obj);   /* on (or onto) the slot: the routine runs from there */
            return 0;
        }
        int s = ex > 64 || ex < -64 || ey > 64 || ey < -64 ? 32 : 16;
        dx = ex > s ? s : ex < -s ? -s : ex;
        dy = ey > s ? s : ey < -s ? -s : ey;
        try_step(w, x, y, &dx, &dy);
    }
    /* the leash: more than a screen from the leader */
    int ox = (x + dx - rd16(w, OBJ_X + lead)) / 16, oy = (y + dy - rd16(w, OBJ_Y + lead)) / 16;
    if (ox > SCREEN_W || ox < -SCREEN_W || oy > SCREEN_H || oy < -SCREEN_H) {
        snap(w, obj);
        return 0;
    }
    /* the routine's work, for this step: velocity, animation, priority */
    w[OBJ_VX + obj] = (uint8_t)dx;
    w[OBJ_VY + obj] = (uint8_t)dy;
    set_move_anim(w, obj, dx, dy);
    unsigned tx = (unsigned)((x + dx) >> 8) & w[F_WRAP_X];
    unsigned ty = (unsigned)((y + dy) >> 8) & w[F_WRAP_Y];
    w[OBJ_PRIO_TOP + obj] = (uint8_t)((w[MAP_PROP_A + ty * 256 + tx] & 0x40) >> 2 | 0x20);
    w[OBJ_PRIO_BOT + obj] = (uint8_t)((w[MAP_PROP_B + ty * 256 + tx] & 0x40) >> 2 | 0x20);
    f1.x = x + dx;   /* where Obj_ApplyMoveVelocity will put it */
    f1.y = y + dy;
    op_rts(cpu);
    return 1;
}
