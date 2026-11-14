/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The sprite decoder (game/ct/sprites.c) against what the console drew.
 *
 * Plays a replay and, at each checkpoint frame's edge, decodes every field
 * object from ROM and WRAM, then checks each object the game has in OAM
 * (ct_scene's object list says which entries draw it):
 *   1. pieces: the decoded image, placed at the object's screen position,
 *      has the same color index at every pixel as the object's own OAM
 *      entries drawn from VRAM (all of them, even where hidden);
 *   2. colors: every used color index has the color its OAM palette has in
 *      CGRAM;
 *   3. screen: wherever the PPU's layer buffer shows a sprite pixel and
 *      this object's entry is the front one there, the frame's pixel is
 *      the decoded color (pixels changed by color math are left out);
 *   4. frame: for objects on screen, the frame worked out from the
 *      animation state is the one the game built.
 * Objects off the screen are decoded too (they must decode), and an object
 * seen off the screen at one checkpoint and on it at a later one, with
 * the same frame, must have decoded to the same image both times.
 *
 * usage: test_sprites SCRIPT [--poke F:ADDR^M] [--png DIR] FRAME...
 * Exit 0 if every check passed; 1 otherwise. ROM from $CT_ROM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "interp.h"
#include "module.h"
#include "png.h"
#include "replay.h"
#include "sched.h"
#include "sprites.h"

#define MAXF 32
#define SIDE CT_SPRITE_MAX_SIDE

static replay input;
static long frames[MAXF];
static int n_frames, failed;
static long poke_frame = -1;
static unsigned poke_addr, poke_mask;
static const char *png_dir;

/* Off-screen decodes kept for the later on-screen comparison. */
typedef struct {
    int valid;
    uint32_t key;
    uint8_t img[SIDE * SIDE];
    uint16_t w, h;
    long frame;
    int never_built;         /* the game had never built a frame for it */
} kept;
static kept offscreen[64];
static int offscreen_matched, offscreen_never;

static unsigned w8(unsigned a) { return bus_wram()[a]; }
static unsigned w16(unsigned a) { return w8(a) | w8(a + 1) << 8; }

static uint8_t idx[SIDE * SIDE];

/* What one checkpoint saw, for the screen check one frame later: the
   frame drawn in frame F+1 is the OAM and VRAM the game uploaded in frame
   F's VBlank, which are the console's state at frame F's edge. */
typedef struct {
    int obj, ox, oy;
    uint16_t w, h;
    uint16_t palette[16];
    uint8_t n_pieces, piece[CT_SCENE_MAX_PIECES];
    uint8_t img[SIDE * SIDE];
} drawn;
static drawn shown[64];
static int n_shown;
static long shown_frame = -1;
static ct_scene shown_scene;
static uint16_t shown_vram[0x8000], shown_oam[272], shown_cgram[256];
static long scr_ok, scr_math;


/* Color index of OAM entry e at screen pixel (sx, sy), -1 if outside it. */
static int oam_pixel(const ct_scene *s, int e, int sx, int sy, int *pal)
{
    unsigned w0 = s->oam[2 * e], w1 = s->oam[2 * e + 1];
    unsigned hi = (s->oam[0x100 + e / 8] >> (2 * (e % 8))) & 3;
    int size = s->obj_size[(hi >> 1) & 1];
    int x = (int)(w0 & 0xFF) | (hi & 1 ? -256 : 0), y = (int)(w0 >> 8);
    int dx = sx - x, dy = (sy - y) & 0xFF;
    if (dx < 0 || dx >= size || dy >= size)
        return -1;
    if (w1 & 0x4000)
        dx = size - 1 - dx;
    if (w1 & 0x8000)
        dy = size - 1 - dy;
    unsigned tile = w1 & 0x1FF, row = (tile >> 4 & 15) + (unsigned)(dy >> 3);
    unsigned col = (tile & 15) + (unsigned)(dx >> 3);
    unsigned t = (tile & 0x100) | (row & 15) << 4 | (col & 15);
    unsigned base = (s->obj_chr[t >> 8] + (t & 0xFF) * 16) & 0x7FFF;
    int r = dy & 7, bit = 7 - (dx & 7);
    unsigned p01 = s->vram[(base + r) & 0x7FFF], p23 = s->vram[(base + 8 + r) & 0x7FFF];
    *pal = (int)(w1 >> 9 & 7);
    return (int)((p01 >> bit & 1) | (p01 >> (bit + 8) & 1) << 1 | (p23 >> bit & 1) << 2 |
                 (p23 >> (bit + 8) & 1) << 3);
}

static unsigned scale(unsigned c, unsigned bright)
{
    c = c * bright / 15;
    return c << 3 | c >> 2;
}

static int worked_out(int obj);

/* --png: the decoded frame as an image (ROM-derived: keep it in the build
   tree). */
static void save_png(long f, int obj, const char *where)
{
    static uint8_t rgba[SIDE * SIDE * 4], bgrx[SIDE * SIDE * 4];
    ct_sprite_info in;
    if (!png_dir || ct_sprite_frame(obj, rgba, SIDE * SIDE, &in))
        return;
    for (int k = 0; k < in.w * in.h; k++) {
        bgrx[4 * k] = rgba[4 * k + 2];
        bgrx[4 * k + 1] = rgba[4 * k + 1];
        bgrx[4 * k + 2] = rgba[4 * k];
        bgrx[4 * k + 3] = 0;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/f%ld_obj%02d_%s_%06X.png", png_dir, f, obj, where, in.key);
    png_write_bgrx(path, bgrx, in.w, in.h);
}

static void check_frame(long f)
{
    static ct_scene s;
    memset(&s, 0, sizeof s);
    if (ct_scene_capture(&s) || s.mode != CT_SCENE_FIELD) {
        printf("frame %ld: not a field scene\n", f);
        failed = 1;
        return;
    }
    int n_slots = (int)w8(0x12000) < 64 ? (int)w8(0x12000) : 64;
    int sc_index = 0, on = 0, off = 0, bad_here = 0;
    long px_ok = 0, hi_colors = 0;
    int frame_same = 0, frame_diff = 0;
    n_shown = 0;
    shown_frame = f;
    shown_scene = s;
    memcpy(shown_vram, s.vram, sizeof shown_vram);
    memcpy(shown_oam, s.oam, sizeof shown_oam);
    memcpy(shown_cgram, s.cgram, sizeof shown_cgram);
    shown_scene.vram = shown_vram;
    shown_scene.oam = shown_oam;
    shown_scene.cgram = shown_cgram;
    for (int obj = 0; obj < n_slots; obj++) {
        unsigned type = w8(0x1100 + 2 * obj);
        if ((type & 0x80) || type == 7)
            continue;
        const ct_scene_object *so = &s.object[sc_index++];   /* scene.c's order */
        ct_sprite_info in;
        if (ct_sprite_frame_indexed(obj, idx, (int)sizeof idx, &in)) {
            int fr = ct_sprite_current_frame(obj, NULL);
            if (fr >= 0) {
                printf("frame %ld obj %d: frame %d does not decode (format %u)\n", f, obj, fr,
                       w8(0x1201 + 2 * obj) & 3);
                bad_here++;
            }
            continue;
        }
        if (in.x != so->x || in.y != so->y) {
            printf("frame %ld obj %d: position %d,%d, scene %d,%d\n", f, obj, (int)in.x,
                   (int)in.y, (int)so->x, (int)so->y);
            bad_here++;
        }
        if (!so->n_pieces) {
            /* Off screen: keep it for a later checkpoint. */
            off++;
            kept *k = &offscreen[obj];
            k->valid = 1;
            k->key = in.key;
            k->w = in.w;
            k->h = in.h;
            k->frame = f;
            k->never_built = w8(0x0F01 + 2 * obj) == 0xFF;
            memcpy(k->img, idx, (size_t)in.w * in.h);
            save_png(f, obj, "off");
            continue;
        }
        on++;
        for (int k2 = 0; k2 < in.w * in.h; k2++)
            hi_colors += idx[k2] >= 13;
        /* 4: the animation state gives the frame the game built, or one
           the game builds at the next tick of the animation timer (a
           facing or animation changed since the last one). */
        unsigned o = 2 * (unsigned)obj;
        int computed = worked_out(obj);
        if (computed == in.frame)
            frame_same++;
        else {
            frame_diff++;
            printf("frame %ld obj %d: built frame %u, animation state gives %d (timer %u): "
                   "built at the next tick\n", f, obj, in.frame, computed, w8(0x1601 + o));
        }
        /* Screen position of the foot point (Obj_ComputeScreenPos). */
        int fx = (int)(w16(0x0A00 + o) & 0x1FF), fy = (int)(w16(0x0A80 + o) & 0x1FF);
        if (fx >= 256)
            fx -= 512;
        int ox = fx - in.origin_x, oy = fy - in.origin_y;
        /* 1 and 2: own OAM entries vs the decoded image, every pixel of
           the union of both. */
        int mism = 0;
        for (int y = -16; y < in.h + 16; y++)
            for (int x = -16; x < in.w + 16; x++) {
                int sx = ox + x, sy = oy + y, got = 0, pal = -1;
                if (sx < 0 || sy < 0 || sx >= SCHED_WIDTH || sy >= SCHED_HEIGHT)
                    continue;   /* off the screen: OAM entries there are dropped */
                if (x >= 0 && y >= 0 && x < in.w && y < in.h)
                    got = idx[y * in.w + x];
                int want = 0;
                for (int p = so->n_pieces - 1; p >= 0; p--) {   /* front entry last */
                    int pp, c = oam_pixel(&s, so->piece[p], sx, sy, &pp);
                    if (c > 0) {
                        want = c;
                        pal = pp;
                    }
                }
                if (got != want) {
                    if (mism++ < 3)
                        printf("frame %ld obj %d: pixel %d,%d decoded %d, OAM %d\n", f, obj, x, y,
                               got, want);
                } else if (want) {
                    px_ok++;
                    unsigned cg = s.cgram[128 + 16 * pal + want] & 0x7FFF;
                    if (cg != in.palette[want]) {
                        if (mism++ < 3)
                            printf("frame %ld obj %d: color %d is %04X, CGRAM %04X\n", f, obj,
                                   want, in.palette[want], cg);
                    }
                }
            }
        if (mism) {
            printf("frame %ld obj %d (frame %u key %06X): %d pixels differ\n", f, obj, in.frame,
                   in.key, mism);
            bad_here++;
        }
        /* An object decoded off screen earlier, now drawn with the same
           frame: the same image. */
        kept *k = &offscreen[obj];
        if (k->valid && k->key == in.key) {
            if (k->w != in.w || k->h != in.h || memcmp(k->img, idx, (size_t)in.w * in.h)) {
                printf("frame %ld obj %d: decoded off screen at %ld, differs on screen\n", f, obj,
                       k->frame);
                bad_here++;
            } else {
                printf("frame %ld obj %d: decoded off screen at frame %ld%s, the same image on "
                       "screen now (key %06X)\n", f, obj, k->frame,
                       k->never_built ? " before the game ever built a frame for it" : "",
                       in.key);
                offscreen_never += k->never_built;
                offscreen_matched++;
            }
        }
        k->valid = 0;
        drawn *d = &shown[n_shown++];
        d->obj = obj;
        d->ox = ox;
        d->oy = oy;
        d->w = in.w;
        d->h = in.h;
        memcpy(d->palette, in.palette, sizeof d->palette);
        d->n_pieces = so->n_pieces;
        memcpy(d->piece, so->piece, sizeof d->piece);
        memcpy(d->img, idx, (size_t)in.w * in.h);
        save_png(f, obj, "on");
    }
    printf("frame %ld: %d objects on screen, %d off; %ld piece pixels match; frame from "
           "animation state %d/%d; %ld pixels use colors 13-15\n", f, on, off, px_ok, frame_same,
           frame_same + frame_diff, hi_colors);
    if (on < 3) {
        printf("frame %ld: only %d objects on screen\n", f, on);
        bad_here++;
    }
    if (bad_here)
        failed = 1;
}

/* 3: frame F+1's pixels where an object's entry (from frame F) is in front. */
static void check_screen(long f)
{
    const uint8_t *frame = sched_frame(), *layers = sched_layers();
    unsigned bright = shown_scene.brightness;
    int bad = 0;
    long ok0 = scr_ok;
    for (int n = 0; n < n_shown; n++) {
        const drawn *d = &shown[n];
        /* 3: the PPU's pixels where this object's entry is in front. */
        for (int y = 0; y < d->h; y++)
            for (int x = 0; x < d->w; x++) {
                int sx = d->ox + x, sy = d->oy + y;
                if (sx < 0 || sy < 0 || sx >= SCHED_WIDTH || sy >= SCHED_HEIGHT)
                    continue;
                unsigned lay = layers[sy * SCHED_WIDTH + sx];
                if ((lay & 7) != 4 && (lay & 7) != 6)
                    continue;
                int front = -1, pal;
                for (int e = 0; e < 128 && front < 0; e++)
                    if (oam_pixel(&shown_scene, e, sx, sy, &pal) > 0)
                        front = e;
                int mine = 0;
                for (int p = 0; p < d->n_pieces; p++)
                    mine |= d->piece[p] == front;
                if (!mine)
                    continue;
                if (lay & 8) {
                    scr_math++;
                    continue;
                }
                unsigned c = d->palette[d->img[y * d->w + x]];
                const uint8_t *q = frame + 4 * (sy * SCHED_WIDTH + sx);   /* B G R */
                if (!d->img[y * d->w + x] || q[2] != scale(c & 31, bright) ||
                    q[1] != scale(c >> 5 & 31, bright) ||
                    q[0] != scale(c >> 10 & 31, bright)) {
                    if (bad < 20)
                        printf("frame %ld obj %d: screen %d,%d is %02X%02X%02X, decoded %04X "
                               "(index %d)\n", f, d->obj, sx, sy, q[2], q[1], q[0], c,
                               d->img[y * d->w + x]);
                    bad++;
                } else
                    scr_ok++;
            }
    }
    printf("frame %ld: %ld screen pixels match the decoded colors (%ld under color math "
           "skipped)\n", f, scr_ok - ok0, scr_math);
    if (bad || scr_ok == ok0) {
        printf("frame %ld: %d screen pixels differ\n", f, bad);
        failed = 1;
    }
}

/* Check 4 on every frame from the first checkpoint on: whenever the game
   builds a new frame for an object on screen ($0F01 changes), the
   animation state just after must select that frame. */
static int worked_out(int obj)
{
    unsigned o = 2 * (unsigned)obj;
    uint8_t save = bus_wram()[0x0F00 + o];
    bus_wram()[0x0F00 + o] = 0;   /* ask for the frame from the animation state */
    int fr = ct_sprite_current_frame(obj, NULL);
    bus_wram()[0x0F00 + o] = save;
    return fr;
}

static int last_built[64];
static long builds_ok, builds_bad;

static void follow_builds(long f)
{
    int n = (int)w8(0x12000) < 64 ? (int)w8(0x12000) : 64;
    for (int obj = 0; obj < 64; obj++) {
        unsigned o = 2 * (unsigned)obj, t = w8(0x1100 + o);
        int built = obj < n && !(t & 0x80) && t != 7 && (w8(0x0F00 + o) & 0x80)
                        ? (int)w8(0x0F01 + o) : -1;
        if (built >= 0 && built != 0xFF && last_built[obj] >= 0 && built != last_built[obj]) {
            int fr = worked_out(obj);
            if (fr == built)
                builds_ok++;
            else {
                if (builds_bad++ < 10)
                    printf("frame %ld obj %d: built frame %d, animation state gives %d\n", f, obj,
                           built, fr);
            }
        }
        last_built[obj] = built;
    }
}

static void on_frame(long f)
{
    if (n_frames && f >= frames[0] - 300)
        follow_builds(f);
    sched_set_joypad(0, replay_buttons(&input, f + 1));
    if (f == poke_frame)
        bus_wram()[poke_addr] ^= (uint8_t)poke_mask;
    if (f == shown_frame + 1)
        check_screen(f);
    for (int k = 0; k < n_frames; k++)
        if (frames[k] == f)
            check_frame(f);
}

int main(int argc, char **argv)
{
    if (argc < 3 || replay_load(&input, argv[1])) {
        fprintf(stderr, "usage: test_sprites SCRIPT [--poke F:ADDR^M] [--png DIR] FRAME...\n");
        return 2;
    }
    for (int k = 2; k < argc; k++) {
        if (!strcmp(argv[k], "--poke") && k + 1 < argc) {
            if (sscanf(argv[++k], "%ld:%x^%x", &poke_frame, &poke_addr, &poke_mask) != 3 ||
                poke_addr >= CT_WRAM_SIZE)
                return 2;
        } else if (!strcmp(argv[k], "--png") && k + 1 < argc)
            png_dir = argv[++k];
        else if (n_frames < MAXF)
            frames[n_frames++] = atol(argv[k]);
    }
    for (int k = 0; k < 64; k++)
        last_built[k] = -1;
    long last = 0;
    for (int k = 0; k < n_frames; k++)
        last = frames[k] + 1 > last ? frames[k] + 1 : last;
    static CPU cpu;
    bus_init(NULL);
    interp_reset(&cpu);
    sched_init(&cpu);
    sched_set_frame_hook(on_frame);
    sched_set_joypad(0, replay_buttons(&input, 1));
    while (sched_frame_count() < last + 64)
        sched_run_frame();
    printf("new frames built for objects on screen: %ld given by the animation state, %ld "
           "not\n", builds_ok, builds_bad);
    if (builds_bad || !builds_ok)
        failed = 1;
    if (offscreen_matched)
        printf("%d objects decoded off screen matched their later on-screen image (%d never "
               "built by the game when decoded)\n", offscreen_matched, offscreen_never);
    printf(failed ? "test_sprites: FAILED\n" : "test_sprites: ok\n");
    return failed;
}
