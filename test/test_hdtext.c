/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: HD text (frontend/hdtext.h) on a made-up text box: a line of
 * 32 x 2 tiles at an odd place on the screen, its pixels in the
 * provenance G-buffer, characters from a ct_text. The font is one found
 * on this system at run time ($CT_TEST_FONT first); none: skipped (77).
 * No game data: the "glyphs" are boxes this test draws.
 *
 * usage: test_hdtext */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "hdtext.h"

#define W SCHED_WIDTH
#define H SCHED_HEIGHT

static const char *find_font(void)
{
    static const char *const cand[] = {
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf",
        "/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    const char *env = getenv("CT_TEST_FONT");
    if (env && *env)
        return env;
    for (size_t k = 0; k < sizeof cand / sizeof cand[0]; k++) {
        FILE *f = fopen(cand[k], "rb");
        if (f) {
            fclose(f);
            return cand[k];
        }
    }
    return NULL;
}

static sched_prov_px prov[W * H];
static sched_vmem mem;
static uint8_t px[W * H * 4], layers[W * H];
static ct_text txt;

/* The line's tiles at (ox, oy): characters $5800 + ..., palette 8; a
   "glyph" is a filled 5 x 8 box with a shadow, at each glyph's x. */
static void scene(int ox, int oy)
{
    memset(prov, 0, sizeof prov);
    for (int k = 0; k < W * H; k++) {   /* the box's background: a pattern */
        px[4 * k] = (uint8_t)(40 + (k * 7) % 23);
        px[4 * k + 1] = (uint8_t)(30 + (k * 3) % 17);
        px[4 * k + 2] = 20;
        layers[k] = 0x10;
        prov[k] = (sched_prov_px){.chr = 0x1000, .pal = 0, .cidx = 4, .flags = 2, .index = 0};
    }
    ct_text_line *ln = &txt.line[0];
    for (int g = 0; g < ln->n; g++)
        for (int y = 0; y < 9; y++)
            for (int x = 0; x < 6; x++) {
                int lx = ln->g[g].x + x, ly = ln->top + y;
                int sx = ox + lx, sy = oy + ly;
                if (sx < 0 || sy < 0 || sx >= W || sy >= H)
                    continue;
                int shadow = x == 5 || y == 8;
                int i = sy * W + sx;
                px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = shadow ? 10 : 250;
                layers[i] = 0x22;
                prov[i] = (sched_prov_px){.chr = ln->chr[ly / 8][lx / 8], .pal = 8,
                                          .cidx = (uint8_t)(shadow ? 9 : 11), .flags = 1,
                                          .u = (uint8_t)(lx % 8), .v = (uint8_t)(ly % 8),
                                          .index = 2};
            }
}

int main(void)
{
    hdtext_result r;
    memset(&txt, 0, sizeof txt);
    txt.layer = 2;
    txt.fg = 3;
    txt.shadow = 1;
    txt.n_lines = 1;
    ct_text_line *ln = &txt.line[0];
    ln->cols = 32, ln->rows = 2, ln->top = 4, ln->height = 12;
    for (int c = 0; c < 32; c++) {
        ln->chr[0][c] = (uint16_t)(0x5800 + 8 * c);
        ln->chr[1][c] = (uint16_t)(0x5800 + 8 * (c + 32));
    }
    const char *word = "Hello world";
    for (int k = 0; word[k]; k++)
        ln->g[ln->n++] = (ct_text_glyph){.ch = (uint8_t)word[k], .x = (int16_t)(8 + 7 * k), .w = 7};
    ln->g[ln->n++] = (ct_text_glyph){.ch = 0, .x = (int16_t)(8 + 7 * 11), .w = 7};   /* unnamed */
    mem.cgram[8 + 3] = 0x7FFF;   /* white */
    mem.cgram[8 + 1] = 0x0000;

    /* No font: nothing happens. */
    scene(20, 150);
    CHECK(!hdtext_ready(), "no font by default");
    int n = hdtext_frame(&txt, prov, &mem, 15, px, layers, 4, &r);
    CHECK(n == 0 && r.bgrx == px && r.layers == layers && !r.rgba,
          "no font: the picture as it was, no overlay");

    const char *font = find_font();
    if (!font) {
        printf("test_hdtext: no font on this system: skipped (set CT_TEST_FONT)\n");
        return 77;
    }
    char err[512];
    CHECK(hdtext_load_font("/nonexistent/font.ttf", err, sizeof err) == -1,
          "a missing font is refused: %s", err);
    CHECK(hdtext_load_font(font, err, sizeof err) == 0, "font %s loads", font);

    n = hdtext_frame(&txt, prov, &mem, 15, px, layers, 4, &r);
    CHECK(n == 1 && r.rgba, "one line drawn, with an overlay");
    CHECK(r.x == 20 && r.y == 150 && r.cw == 236 && r.ch == 18 && r.w == 236 * 4 && r.h == 18 * 4,
          "the overlay covers the line on screen: %.0f %.0f %.0f %.0f, %d x %d", r.x, r.y, r.cw,
          r.ch, r.w, r.h);
    /* The console's glyph pixels are gone from the picture (the box's
       colors instead); the rest is untouched; the unnamed glyph stays. */
    int left = 0, changed_outside = 0, kept = 0;
    for (int k = 0; k < W * H; k++) {
        int glyph = prov[k].index == 2, white = r.bgrx[4 * k] == 250;
        int x = k % W - 20;
        if (glyph && x < 8 + 7 * 11 && (white || r.bgrx[4 * k] == 10))
            left++;
        if (glyph && x >= 8 + 7 * 11 && white)
            kept++;
        if (!glyph && memcmp(r.bgrx + 4 * k, px + 4 * k, 4))
            changed_outside++;
    }
    CHECK(left == 0, "the console's glyphs taken out: %d pixels left", left);
    CHECK(kept > 0, "a glyph with no character keeps the console's: %d pixels", kept);
    CHECK(changed_outside == 0, "nothing else changes: %d pixels", changed_outside);
    /* The overlay draws text (white, opaque somewhere) over the first
       glyphs, and nothing before the line's first glyph. */
    int ink = 0, before = 0;
    for (int y = 0; y < r.h; y++)
        for (int x = 0; x < r.w; x++) {
            const uint8_t *t = r.rgba + ((size_t)y * r.w + x) * 4;
            if (t[3] > 200 && t[0] > 200)
                ink++;
            if (x < 7 * 4 && t[3])
                before++;
        }
    CHECK(ink > 200, "the font's text is in the overlay: %d bright texels", ink);
    CHECK(before == 0, "nothing left of the first glyph: %d texels", before);

    /* The same frame again: the same overlay (not drawn again). */
    unsigned gen = r.gen;
    hdtext_frame(&txt, prov, &mem, 15, px, layers, 4, &r);
    CHECK(r.gen == gen, "unchanged text: the same overlay (gen %u)", r.gen);
    /* The box moves: the overlay follows. */
    scene(30, 40);
    hdtext_frame(&txt, prov, &mem, 15, px, layers, 4, &r);
    CHECK(r.gen != gen && r.x == 30 && r.y == 40, "the box moved: the overlay at %.0f, %.0f",
          r.x, r.y);
    /* Off the screen: no line, no overlay. */
    memset(prov, 0, sizeof prov);
    n = hdtext_frame(&txt, prov, &mem, 15, px, layers, 4, &r);
    CHECK(n == 0 && !r.rgba && r.bgrx == px, "a line not on the screen isn't drawn");
    /* Outline and plain styles draw too. */
    scene(20, 150);
    for (int st = HDTEXT_OUTLINE; st <= HDTEXT_PLAIN; st++) {
        hdtext_set_style(st);
        n = hdtext_frame(&txt, prov, &mem, 15, px, layers, 3, &r);
        CHECK(n == 1 && r.rgba && r.w == 236 * 3, "style %d draws", st);
    }
    hdtext_load_font(NULL, err, sizeof err);
    CHECK(!hdtext_ready(), "font unloaded");
    return th_report("hdtext");
}
