/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: message masks with glyphs (osd_text.h). */
#include "osd_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glyphs.h"
#include "hd2d/osd_font.h"

#if GLYPH_PX != OSD_GLYPH_UNITS * OSD_OS
#error "glyph size and OSD units disagree"
#endif

/* One piece of a message: a character, or a token's glyph / label. */
typedef struct {
    const uint8_t *glyph;   /* NULL: text */
    const char *text;       /* the label, or a single character */
    size_t len;
} piece;

/* The next piece at *p, advancing it. */
static int next_piece(const char **p, piece *out, char *tok, size_t tokn)
{
    const char *s = *p;
    if (!*s)
        return 0;
    if (*s == '[') {
        const char *e = strchr(s, ']'), *o = strchr(s + 1, '[');
        if (e && (!o || o > e) && (size_t)(e - s - 1) < tokn && e > s + 1) {
            memcpy(tok, s + 1, (size_t)(e - s - 1));
            tok[e - s - 1] = 0;
            const char *label;
            out->glyph = glyphs_lookup(tok, &label);
            out->text = label;
            out->len = strlen(label);
            *p = e + 1;
            return 1;
        }
    }
    out->glyph = NULL;
    out->text = s;
    out->len = 1;
    *p = s + 1;
    return 1;
}

static int piece_units(const piece *pc)
{
    return pc->glyph ? OSD_GLYPH_UNITS : 6 * (int)pc->len;
}

int osd_text_mask(const char *text, uint8_t **mask, int *w, int *h)
{
    char tok[40];
    piece pc;
    int units = 3;   /* 2 in front, 1 behind */
    for (const char *p = text; next_piece(&p, &pc, tok, sizeof tok);)
        units += piece_units(&pc);
    int W = units * OSD_OS, H = OSD_UNITS_H * OSD_OS;
    if (W > OSD_MAX_TEXELS)
        W = OSD_MAX_TEXELS;
    uint8_t *m = calloc((size_t)W * H, 1);
    if (!m)
        return -1;
    int ux = 2;
    for (const char *p = text; next_piece(&p, &pc, tok, sizeof tok);) {
        if (pc.glyph) {
            for (int y = 0; y < GLYPH_PX; y++)
                for (int x = 0; x < GLYPH_PX; x++) {
                    int tx = ux * OSD_OS + x;
                    if (tx < W)
                        m[y * W + tx] = pc.glyph[y * GLYPH_PX + x];
                }
        } else {
            for (size_t k = 0; k < pc.len; k++) {
                const uint8_t *rows = osd_glyph(pc.text[k]);
                for (int y = 0; y < 7; y++)
                    for (int x = 0; x < 5; x++) {
                        if (!(rows[y] & (0x10 >> x)))
                            continue;
                        for (int sy = 0; sy < OSD_OS; sy++)
                            for (int sx = 0; sx < OSD_OS; sx++) {
                                int tx = (ux + 6 * (int)k + x) * OSD_OS + sx;
                                if (tx < W)
                                    m[((y + 2) * OSD_OS + sy) * W + tx] = 255;
                            }
                    }
            }
        }
        ux += piece_units(&pc);
    }
    *mask = m;
    *w = W;
    *h = H;
    return 0;
}

void osd_text_plain(const char *text, char *out, size_t n)
{
    char tok[40];
    piece pc;
    size_t o = 0;
    if (!n)
        return;
    for (const char *p = text; next_piece(&p, &pc, tok, sizeof tok);)
        for (size_t k = 0; k < pc.len && o + 1 < n; k++)
            out[o++] = pc.text[k];
    out[o] = 0;
}

/* ---- the plain picture's message ---- */

#define OSD_FRAMES 130   /* as hd2d.c */

static struct {
    char text[160];
    int frames, dirty, w, h;
    SDL_Texture *tex;
    SDL_Renderer *ren;
} sdl;

void osd_sdl_show(const char *text)
{
    snprintf(sdl.text, sizeof sdl.text, "%s", text);
    sdl.frames = OSD_FRAMES;
    sdl.dirty = 1;
}

static void sdl_upload(SDL_Renderer *ren)
{
    uint8_t *mask;
    int w, h;
    sdl.dirty = 0;
    if (sdl.tex)
        SDL_DestroyTexture(sdl.tex);
    sdl.tex = NULL;
    sdl.ren = ren;
    if (osd_text_mask(sdl.text, &mask, &w, &h))
        return;
    uint32_t *px = malloc((size_t)w * h * 4);
    sdl.tex = px ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h)
                 : NULL;
    if (sdl.tex) {
        for (int k = 0; k < w * h; k++)   /* white, the mask as alpha */
            px[k] = (uint32_t)mask[k] << 24 | 0xFFFFFFu;
        SDL_UpdateTexture(sdl.tex, NULL, px, w * 4);
        SDL_SetTextureBlendMode(sdl.tex, SDL_BLENDMODE_BLEND);
        sdl.w = w;
        sdl.h = h;
    }
    free(px);
    free(mask);
}

void osd_sdl_draw(SDL_Renderer *ren, int x, int y, int scale, int tick)
{
    if (sdl.frames <= 0)
        return;
    if (sdl.dirty || sdl.ren != ren)
        sdl_upload(ren);
    float a = sdl.frames < 20 ? sdl.frames / 20.0f : 1.0f;
    if (tick)
        sdl.frames--;
    if (!sdl.tex)
        return;
    SDL_Rect r = {x, y, sdl.w * scale / OSD_OS, sdl.h * scale / OSD_OS};
    /* As hd2d's final pass: the strip at 30% brightness, the mask white. */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(0.7f * a * 255.0f));
    SDL_RenderFillRect(ren, &r);
    SDL_SetTextureAlphaMod(sdl.tex, (Uint8)(a * 255.0f));
    SDL_RenderCopy(ren, sdl.tex, NULL, &r);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

void osd_sdl_shutdown(void)
{
    if (sdl.tex)
        SDL_DestroyTexture(sdl.tex);
    sdl.tex = NULL;
    sdl.ren = NULL;
}
