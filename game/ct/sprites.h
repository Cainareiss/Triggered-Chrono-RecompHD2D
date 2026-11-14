/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Field object sprites (docs/SCENE.md A1): any object's animation frame
 * drawn from the ROM and the object table in WRAM, whether or not the game
 * put it in OAM. Reads only; call at the frame edge. */
#ifndef CT_GAME_SPRITES_H
#define CT_GAME_SPRITES_H

#include <stdint.h>

#define CT_SPRITE_MAX_PIECES 12   /* 16x16 pieces in the largest frame format */
#define CT_SPRITE_MAX_SIDE   288  /* piece offsets are signed bytes: -128..127+16 */

enum { CT_SPRITE_PC = 0, CT_SPRITE_NPC = 1, CT_SPRITE_ENEMY = 2, CT_SPRITE_OTHER = 3 };

typedef struct {
    int16_t dx, dy;          /* top-left, relative to the foot point, px */
    uint8_t top;             /* 1: drawn with the object's top-half priority
                                ($0C00), 0: bottom half ($0C01) */
    uint16_t tile[4];        /* source tile words TL TR BL BR: tile 0-10, h flip 14 */
} ct_sprite_piece;

typedef struct {
    uint16_t w, h;           /* the image: the pieces' bounding box, px */
    int16_t origin_x, origin_y;   /* the foot point inside the image, px */
    int32_t x, y;            /* the foot point on the map, px */
    uint8_t frame;           /* frame number in the object's assembly table */
    uint8_t from_oam;        /* 1: the frame the game last built for it ($0F01);
                                0: worked out from its animation state */
    uint8_t format;          /* $1201 & 3: 0 = 4 pieces, 1 = 8 */
    uint8_t n_pieces;
    ct_sprite_piece piece[CT_SPRITE_MAX_PIECES];
    uint16_t palette[16];    /* BGR555; 0 is transparent */
    /* Stable key for texture packs: the same pose on any map, in any
       slot: what was loaded (kind and id, i.e. the sprite record) and
       the frame. */
    uint8_t kind;            /* CT_SPRITE_PC ... */
    uint8_t id;              /* PC, NPC or enemy id ($1101) */
    uint32_t key;            /* kind << 16 | id << 8 | frame */
    uint32_t record;         /* ROM address of its sprite record (0: unknown) */
    uint32_t palette_src;    /* ROM address of colors 1-12 */
} ct_sprite_info;

/* The animation frame the object shows now, or would show if on screen:
   its last built frame when it has one and is active, else the frame its
   animation state (facing, animation, step) selects in the bank-$E4
   table. -1 if the slot is empty. */
int ct_sprite_current_frame(int obj, int *from_oam);

/* Describe object obj's current frame (pieces, palette, key). 0, or -1 if
   the slot is empty, has no frame, or uses a format this can't draw. */
int ct_sprite_describe(int obj, ct_sprite_info *info);

/* Draw it: color indices (0 transparent), info->w x info->h, row stride
   info->w, into idx (at least max bytes). 0, or -1 as ct_sprite_describe
   or when the image is larger than max. */
int ct_sprite_frame_indexed(int obj, uint8_t *idx, int max, ct_sprite_info *info);

/* Draw it as RGBA8 (R G B A bytes, A 0 or 255), 5-bit colors scaled as
   c << 3 | c >> 2. rgba holds at least max pixels. */
int ct_sprite_frame(int obj, uint8_t *rgba, int max, ct_sprite_info *info);

#endif
