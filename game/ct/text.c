/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HD text (runtime/module.h ct_text): the characters of the dialogue box,
 * line by line, for a renderer that draws them with the player's font.
 * A game patch (funcs.toml `patch`) on the text engine's glyph routine
 * notes each glyph it draws, and changes nothing; ct_game_text gives them
 * at the frame edge with where the box's lines are in VRAM. The
 * interpreter doesn't call patches (--interp-only has no HD text).
 *
 * The text engine (bank $C2, direct page $0200; docs/SCENE.md):
 * Text_EngineTick ($C25823) runs Text_DecodeLoop ($C258B2), which reads the
 * string at [$31]: bytes $A0-$FF are glyphs, smaller ones control codes
 * (names, numbers, line breaks, waits) and dictionary substrings at
 * $DEFA00, which come back through the same loop. Each glyph goes through
 * Text_EmitGlyph ($C25DC4): the code in $35, the pen's x in $34 (px),
 * drawn into a canvas at [$10] ($14 low nibble 0: 2 bits per pixel, else
 * 4), the glyph count in $17. The glyph is 12 x 12 px of the font at
 * $FF2060 (left 8 columns) and $FF3860 (the other 4), 2 planes; its
 * advance comes from the width table at $C260E6.
 *
 * The dialogue box (bank $C0, direct page $0100): Dialog_RenderRequest
 * ($C020F2) renders into the canvas at $7EF000, one line of 32 x 2 tiles
 * (Dialog_InitWindowArea clears it, Text_DrawGlyphRow2bpp's column table
 * at $C25FE6 puts tile column c at byte 16c for c < 16 and 512 + 16(c-16)
 * after, the lower tile row 256 bytes on), glyph rows from 4 px down. The
 * line, 0-3, is $0131 (a new page starts again at 0); it goes to VRAM
 * tiles $100 + 64 * line of BG3's characters ($5000 in the field), which
 * Dialog_BuildTextAreaTilemap ($C02B78) lays out in that order. The
 * glyphs are color 3 with a shadow of color 1 of BG3's palette 2.
 *
 * The character for each glyph code is the game's text encoding (which
 * code is which letter), not its font: the glyphs themselves are drawn
 * from the ROM only by the console. */
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "module.h"

int ct_patch_text_glyph(CPU *cpu);
int ct_game_text(ct_text *out);

enum {
    T_CANVAS = 0x0210,    /* WRAM: long pointer to the canvas */
    T_FORMAT = 0x0214,    /* low nibble: 0 = 2 bits per pixel */
    T_COUNT = 0x0217,     /* glyphs drawn into this line so far */
    T_PEN = 0x0234,       /* the pen's x, px */
    T_CODE = 0x0235,      /* the glyph's code */
    D_LINE = 0x0131,      /* the dialogue box's line, 0-3 */
    WIDTHS = 0x0260E6,    /* ROM offset: advance per glyph code - $A0 */
    CHR_BG3 = 0x5000,     /* VRAM word address of BG3's characters in the field */
    LINE_TILES = 0x100,   /* the box's first line's first tile */
};

static struct {
    uint8_t n;
    uint8_t code[CT_TEXT_GLYPHS];
    uint8_t x[CT_TEXT_GLYPHS];
} lines[CT_TEXT_LINES];

/* The text encoding: glyph code - $A0 to Unicode. */
static uint32_t to_unicode(uint8_t code)
{
    static const uint16_t punct[] = {
        '!', '?', '/', 0x201C, 0x201D, ':', '&', '(', ')', 0x2019, '.', ',', '=', '-',
        '+', '%', 0x266A, ' ', 0x2661, 0x2026, 0x221E, '#',
    };
    if (code < 0xA0)
        return 0;
    unsigned i = code - 0xA0u;
    if (i < 26)
        return 'A' + i;
    if (i < 52)
        return 'a' + (i - 26);
    if (i < 62)
        return '0' + (i - 52);
    i -= 62;
    return i < sizeof punct / sizeof punct[0] ? punct[i] : 0;
}

int ct_patch_text_glyph(CPU *cpu)
{
    (void)cpu;
    const uint8_t *w = bus_wram();
    if (w[T_CANVAS] != 0x00 || w[T_CANVAS + 1] != 0xF0 || w[T_CANVAS + 2] != 0x7E ||
        (w[T_FORMAT] & 0x0F))
        return 0;   /* not the dialogue box's canvas */
    unsigned l = w[D_LINE] & 3;
    if (w[T_COUNT] == 0) {   /* a line starts again: on line 0, a new page */
        if (l == 0)
            memset(lines, 0, sizeof lines);
        lines[l].n = 0;
    }
    if (lines[l].n < CT_TEXT_GLYPHS) {
        lines[l].code[lines[l].n] = w[T_CODE];
        lines[l].x[lines[l].n] = w[T_PEN];
        lines[l].n++;
    }
    return 0;   /* the routine itself runs as translated */
}

int ct_game_text(ct_text *out)
{
    const uint8_t *rom = bus_rom();
    out->layer = 2;   /* BG3 */
    out->fg = 3;
    out->shadow = 1;
    out->n_lines = CT_TEXT_LINES;
    for (int l = 0; l < CT_TEXT_LINES; l++) {
        ct_text_line *o = &out->line[l];
        o->cols = CT_TEXT_COLS;
        o->rows = CT_TEXT_ROWS;
        o->top = 4;
        o->height = 12;
        for (int c = 0; c < CT_TEXT_COLS; c++) {
            unsigned t = LINE_TILES + 64u * (unsigned)l + (c < 16 ? (unsigned)c : 32u + (c - 16u));
            o->chr[0][c] = (uint16_t)(CHR_BG3 + 8 * t);
            o->chr[1][c] = (uint16_t)(CHR_BG3 + 8 * (t + 16));
        }
        o->n = lines[l].n;
        for (int k = 0; k < o->n; k++) {
            uint8_t code = lines[l].code[k];
            o->g[k].ch = to_unicode(code);
            o->g[k].x = lines[l].x[k];
            o->g[k].w = code >= 0xA0 ? rom[WIDTHS + code - 0xA0] : 0;
        }
    }
    return 0;
}
