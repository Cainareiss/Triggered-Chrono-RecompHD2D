/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Field object sprites (sprites.h, docs/SCENE.md A1). The game builds an
 * object's frame only while the object is near the camera: it copies the
 * frame's 8x8 tiles into a WRAM staging buffer, DMAs them to the object's
 * VRAM slot, and writes four (or eight) 16x16 OAM entries. This does the
 * same assembly straight from the source graphics, for any object. */
#include <string.h>

#include "bus.h"
#include "sprites.h"

/* WRAM offsets of the object table (index 2 * slot; docs/SCENE.md A0.6). */
enum {
    OBJ_COUNT = 0x12000,          /* $7F2000 */
    OBJ_ACTIVE = 0x0F00,          /* bit 7: near the camera */
    OBJ_FRAME = 0x0F01,           /* last built frame, $FF none */
    OBJ_TYPE = 0x1100,            /* 0-2 party, 3 scripted PC, 4 NPC, 7 empty, bit 7 removed */
    OBJ_ID = 0x1101,              /* sprite record ($E4F000 + 5 id) */
    OBJ_GFX_BANK = 0x1200,        /* source tiles: bank */
    OBJ_FORMAT = 0x1201,          /* bits 0-1: frame format */
    OBJ_GFX = 0x1280,             /* source tiles: address */
    OBJ_ASM_BANK = 0x1300,        /* frame assembly table: bank */
    OBJ_FIXED_FRAME = 0x1301,     /* the frame when the animation mode is 3 */
    OBJ_ASM = 0x1380,             /* frame assembly table: address */
    OBJ_PALETTE = 0x1400,         /* colors 1-12: address in bank $E4 */
    OBJ_ANIM_SIZE = 0x1480,       /* bytes per facing in the animation table */
    OBJ_ANIM = 0x1500,            /* animation table: address in bank $E4 */
    OBJ_FACING = 0x1600,
    OBJ_ANIM_ID = 0x1680, OBJ_ANIM_STEP = 0x1681,
    OBJ_ANIM_MODE = 0x1780, OBJ_ANIM_ALT = 0x1781,
    OBJ_X = 0x1800, OBJ_Y = 0x1880,   /* 1/16 px */
    OBJ_PAL_SLOT = 0x0F81,        /* OAM attribute palette bits (slot << 1) */
    CGRAM_SHADOW = 0x2000,        /* 256 colors; OBJ palettes from $2100 */
};

static const uint8_t *wram(void) { return bus_wram(); }
static unsigned w8(unsigned a) { return wram()[a]; }
static unsigned w16(unsigned a) { return wram()[a] | wram()[a + 1] << 8; }

/* A byte at a 24-bit address: banks $7E/$7F are WRAM, $C0-$FF the
   (HiROM) ROM. Other banks don't hold sprite data; they read as 0. */
static unsigned at(uint32_t a)
{
    unsigned bank = a >> 16 & 0xFF;
    if (bank == 0x7E || bank == 0x7F)
        return wram()[((bank - 0x7E) << 16) | (a & 0xFFFF)];
    if (bank >= 0xC0)
        return bus_rom()[a & (CT_ROM_SIZE - 1)];
    return 0;
}

static int slot_ok(int obj)
{
    if (obj < 0 || obj >= 64 || obj >= (int)w8(OBJ_COUNT))
        return 0;
    unsigned t = w8(OBJ_TYPE + 2 * obj);
    return !(t & 0x80) && t != 7;
}

/* The frame the animation state selects (Sub_CB3A, $C0CB3A): bank $E4,
   OBJ_ANIM + facing * OBJ_ANIM_SIZE + 4 * animation + step; $FF ends the
   animation (the game goes back to step 0). Mode 2 plays OBJ_ANIM_ALT. */
static int anim_frame(unsigned o)
{
    unsigned mode = w8(OBJ_ANIM_MODE + o);
    if (mode == 3)
        return (int)w8(OBJ_FIXED_FRAME + o);
    unsigned face = w8(OBJ_FACING + o) & 3;
    uint32_t base = 0xE40000u | ((w16(OBJ_ANIM + o) + face * w16(OBJ_ANIM_SIZE + o)) & 0xFFFF);
    unsigned anim = mode == 2 ? w8(OBJ_ANIM_ALT + o) : w8(OBJ_ANIM_ID + o);
    unsigned step = w8(OBJ_ANIM_STEP + o);
    if (step == 0xFF)
        step = 0;   /* set when an animation wraps: step 0 shows until the next tick */
    uint32_t row = 0xE40000u | ((base + 4 * anim) & 0xFFFF);
    unsigned f = at(0xE40000u | ((row + step) & 0xFFFF));
    if (f == 0xFF) {
        if (mode == 2 && w8(OBJ_FRAME + o) != 0xFF)
            return (int)w8(OBJ_FRAME + o);   /* the game keeps the last frame */
        f = at(row);
    }
    return f == 0xFF ? -1 : (int)f;
}

int ct_sprite_current_frame(int obj, int *from_oam)
{
    if (from_oam)
        *from_oam = 0;
    if (!slot_ok(obj))
        return -1;
    unsigned o = 2 * (unsigned)obj;
    if ((w8(OBJ_ACTIVE + o) & 0x80) && w8(OBJ_FRAME + o) != 0xFF) {
        if (from_oam)
            *from_oam = 1;
        return (int)w8(OBJ_FRAME + o);
    }
    return anim_frame(o);
}

int ct_sprite_describe(int obj, ct_sprite_info *s)
{
    memset(s, 0, sizeof *s);
    int from_oam;
    int frame = ct_sprite_current_frame(obj, &from_oam);
    if (frame < 0)
        return -1;
    unsigned o = 2 * (unsigned)obj;
    unsigned format = w8(OBJ_FORMAT + o) & 3;
    if (format > 1)
        return -1;   /* 12-piece frames, built over several frames: not decoded */
    unsigned n = 4 * (format + 1);          /* 16x16 pieces */
    unsigned row = 2 * n;                    /* 8x8 tiles per row in the slot */
    /* A frame record: 4n tile words (two rows of 2n tiles, as the slot
       holds them in VRAM), then n (dx, dy) signed byte pairs. */
    uint32_t rec = (uint32_t)w8(OBJ_ASM_BANK + o) << 16 |
                   ((w16(OBJ_ASM + o) + (unsigned)frame * 10 * n) & 0xFFFF);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (unsigned k = 0; k < n; k++) {
        ct_sprite_piece *p = &s->piece[k];
        static const unsigned q[4] = {0, 1, 0, 1};
        for (unsigned t = 0; t < 4; t++) {
            uint32_t a = rec + 2 * (2 * k + q[t] + (t >= 2 ? row : 0));
            p->tile[t] = (uint16_t)(at(a) | at(a + 1) << 8);
        }
        p->dx = (int8_t)at(rec + 8 * n + 2 * k);
        uint8_t dy = (uint8_t)at(rec + 8 * n + 2 * k + 1);
        p->dy = (int8_t)dy;
        p->top = dy < 0xE8;   /* $C0CD4F: y >= $E8 takes the bottom half's priority */
        if (!k || p->dx < x0)
            x0 = p->dx;
        if (!k || p->dy < y0)
            y0 = p->dy;
        if (!k || p->dx + 16 > x1)
            x1 = p->dx + 16;
        if (!k || p->dy + 16 > y1)
            y1 = p->dy + 16;
    }
    s->n_pieces = (uint8_t)n;
    s->w = (uint16_t)(x1 - x0);
    s->h = (uint16_t)(y1 - y0);
    s->origin_x = (int16_t)-x0;
    s->origin_y = (int16_t)-y0;
    s->x = (int32_t)(w16(OBJ_X + o) >> 4);
    s->y = (int32_t)(w16(OBJ_Y + o) >> 4);
    s->frame = (uint8_t)frame;
    s->from_oam = (uint8_t)from_oam;
    s->format = (uint8_t)format;

    /* Colors 1-12 come from bank $E4 (Obj_AssignSpritePalette, $C07170,
       copies 12 words to the slot's palette); 13-15 are the slot's own,
       taken from the CGRAM shadow while the object holds a slot. */
    s->palette_src = 0xE40000u | w16(OBJ_PALETTE + o);
    for (unsigned c = 1; c <= 12; c++) {
        uint32_t a = s->palette_src + 2 * (c - 1);
        s->palette[c] = (uint16_t)((at(a) | at(a + 1) << 8) & 0x7FFF);
    }
    if (w8(OBJ_ACTIVE + o) & 0x80) {
        unsigned slot = (w8(OBJ_PAL_SLOT + o) >> 1) & 7;
        for (unsigned c = 13; c < 16; c++)
            s->palette[c] = (uint16_t)(w16(CGRAM_SHADOW + 2 * (128 + 16 * slot + c)) & 0x7FFF);
    }
    /* The sprite record the object was loaded from (EventCmd_LoadPc
       $C04476, _LoadNpc $C04590, _LoadEnemy $C046DF). */
    unsigned type = w8(OBJ_TYPE + o);
    s->id = (uint8_t)w8(OBJ_ID + o);
    if (type <= 3) {
        s->kind = CT_SPRITE_PC;
        s->record = 0xE4F000u + 5u * s->id;
    } else if (type == 4) {
        s->kind = CT_SPRITE_NPC;
        s->record = 0xE4F023u + 5u * s->id;
    } else if (type == 5) {
        s->kind = CT_SPRITE_ENEMY;
        s->record = 0xE4F600u + 10u * s->id;
    } else
        s->kind = CT_SPRITE_OTHER;
    s->key = (uint32_t)s->kind << 16 | (uint32_t)s->id << 8 | s->frame;
    return 0;
}

/* One 4bpp 8x8 tile (32 bytes, SNES planar) from the object's source. */
static void draw_tile(uint32_t src, uint16_t word, uint8_t *dst, int stride)
{
    uint32_t a = (src & 0xFF0000u) | ((src + 32u * (word & 0x7FF)) & 0xFFFF);
    int hflip = (word & 0x4000) != 0;   /* $C0E534: bytes bit-reversed */
    for (int r = 0; r < 8; r++) {
        unsigned b0 = at(a + 2 * r), b1 = at(a + 2 * r + 1);
        unsigned b2 = at(a + 16 + 2 * r), b3 = at(a + 17 + 2 * r);
        for (int x = 0; x < 8; x++) {
            int bit = hflip ? x : 7 - x;
            unsigned c = (b0 >> bit & 1) | (b1 >> bit & 1) << 1 | (b2 >> bit & 1) << 2 |
                         (b3 >> bit & 1) << 3;
            if (c)
                dst[r * stride + x] = (uint8_t)c;
        }
    }
}

int ct_sprite_frame_indexed(int obj, uint8_t *idx, int max, ct_sprite_info *s)
{
    if (ct_sprite_describe(obj, s))
        return -1;
    int w = s->w, h = s->h;
    if (w * h > max)
        return -1;
    memset(idx, 0, (size_t)(w * h));
    unsigned o = 2 * (unsigned)obj;
    uint32_t src = (uint32_t)w8(OBJ_GFX_BANK + o) << 16 | w16(OBJ_GFX + o);
    /* Later pieces first: in OAM, entry k draws over entry k+1. */
    for (int k = s->n_pieces - 1; k >= 0; k--) {
        const ct_sprite_piece *p = &s->piece[k];
        int px = p->dx + s->origin_x, py = p->dy + s->origin_y;
        for (int t = 0; t < 4; t++)
            draw_tile(src, p->tile[t], idx + (py + 8 * (t >> 1)) * w + px + 8 * (t & 1), w);
    }
    return 0;
}

int ct_sprite_frame(int obj, uint8_t *rgba, int max, ct_sprite_info *s)
{
    static uint8_t idx[CT_SPRITE_MAX_SIDE * CT_SPRITE_MAX_SIDE];
    if (ct_sprite_frame_indexed(obj, idx, max < (int)sizeof idx ? max : (int)sizeof idx, s))
        return -1;
    for (int k = 0; k < s->w * s->h; k++) {
        uint8_t *d = rgba + 4 * k;
        unsigned c = idx[k], col = s->palette[c];
        unsigned r = col & 31, g = col >> 5 & 31, b = col >> 10 & 31;
        d[0] = (uint8_t)(r << 3 | r >> 2);
        d[1] = (uint8_t)(g << 3 | g >> 2);
        d[2] = (uint8_t)(b << 3 | b >> 2);
        d[3] = c ? 255 : 0;
    }
    return 0;
}
