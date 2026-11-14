/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: simulated rumble: the event queue and the generic detectors
 * (rumble.h). */
#include "rumble.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ppu.h"
#include "snes_adapter.h"

/* ---- the queue ---- */

#define QUEUE 64

static int enabled;
static long cur_frame;
static rumble_ev queue[QUEUE];
static int q_head, q_len;   /* oldest at q_head */

void rumble_set_enabled(int on)
{
    enabled = on != 0;
    if (!enabled)
        q_len = 0;
}

int rumble_enabled(void)
{
    return enabled;
}

static void push(const rumble_ev *e)
{
    if (q_len == QUEUE) {   /* full: the oldest goes */
        q_head = (q_head + 1) % QUEUE;
        q_len--;
    }
    queue[(q_head + q_len) % QUEUE] = *e;
    q_len++;
}

void rumble_event_for(int player, uint16_t low, uint16_t high, uint32_t ms, const char *tag)
{
    if (!enabled || (!low && !high) || !ms)
        return;
    rumble_ev e;
    memset(&e, 0, sizeof e);
    e.frame = cur_frame;
    e.player = (int8_t)(player < 0 || player > 3 ? RUMBLE_ALL : player);
    e.source = RUMBLE_SRC_GAME;
    e.low = low;
    e.high = high;
    e.ms = (uint16_t)(ms > 5000 ? 5000 : ms);
    snprintf(e.tag, sizeof e.tag, "%s", tag ? tag : "event");
    push(&e);
}

void rumble_event(uint16_t low, uint16_t high, uint32_t ms)
{
    rumble_event_for(RUMBLE_ALL, low, high, ms, "event");
}

int rumble_take(rumble_ev *out, int max)
{
    int n = 0;
    while (q_len && n < max) {
        out[n++] = queue[q_head];
        q_head = (q_head + 1) % QUEUE;
        q_len--;
    }
    return n;
}

static ct_module_rumble_fn hook;

void ct_rumble_set_source(ct_module_rumble_fn fn)
{
    hook = fn;
}

/* ---- the detectors ---- */

/* Screen shake: BG1's scroll steps back and forth (at least SHAKE_TURNS
   reversals in the last SHAKE_WINDOW frames) with little net movement, so
   a camera that moves (always one way at a time) never counts. */
#define SHAKE_WINDOW 10
#define SHAKE_TURNS 4
#define SHAKE_MAX_STEP 32   /* larger: a cut, not a shake */
#define SHAKE_REPEAT 6      /* frames between pulses while it shakes */
/* Flash: the picture's light jumps by FLASH_JUMP (of full white) over the
   lowest of the last 4 frames, on a screen already lit (no fade-in from
   black counts). */
#define FLASH_JUMP 0.30f
#define FLASH_LIT 12        /* brightness of the 4 frames before */
#define FLASH_COOL 20
/* Sound: the bass (below about 120 Hz) jumps over its running level. */
#define SOUND_RATIO 3.0f
#define SOUND_FLOOR 0.06f
#define SOUND_COOL 12

void rumble_detect_reset(rumble_detect *d)
{
    int sound = d->sound;
    memset(d, 0, sizeof *d);
    d->sound = sound;
}

static int step16(uint16_t now, uint16_t before)
{
    return ((int)((now - before) & 0x3FF) ^ 0x200) - 0x200;   /* 10-bit scroll, wrapping */
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static uint16_t clamp16(float v)
{
    return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}

static int add(rumble_ev *out, int n, int max, long frame, int src, const char *tag,
               uint16_t low, uint16_t high, int ms)
{
    if (n >= max)
        return n;
    rumble_ev *e = &out[n];
    memset(e, 0, sizeof *e);
    e->frame = frame;
    e->player = RUMBLE_ALL;
    e->source = (uint8_t)src;
    e->low = low;
    e->high = high;
    e->ms = (uint16_t)ms;
    snprintf(e->tag, sizeof e->tag, "%s", tag);
    return n + 1;
}

/* Reversals of direction among the nonzero steps, and the largest step
   and net movement, over the last w entries. */
static void oscillation(const int16_t *s, int w, int *turns, int *amp, int *net)
{
    int last = 0;
    *turns = *amp = *net = 0;
    for (int k = RUMBLE_HISTORY - w; k < RUMBLE_HISTORY; k++) {
        int v = s[k];
        *net += v;
        if (iabs(v) > *amp)
            *amp = iabs(v);
        if (!v)
            continue;
        if (last && (v > 0) != (last > 0))
            ++*turns;
        last = v;
    }
    *net = iabs(*net);
}

int rumble_detect_step(rumble_detect *d, const rumble_frame_in *in, int ctx, long frame,
                       rumble_ev *out, int max)
{
    if (ctx == RUMBLE_CTX_QUIET) {
        rumble_detect_reset(d);
        return 0;
    }
    int n = 0;
    /* shake */
    int dx = 0, dy = 0;
    if (d->n) {
        dx = step16(in->hofs[0], d->px);
        dy = step16(in->vofs[0], d->py);
        if (in->has_camera) {
            dx -= in->camera_x - d->pcx;
            dy -= in->camera_y - d->pcy;
        }
    }
    d->px = in->hofs[0];
    d->py = in->vofs[0];
    d->pcx = in->camera_x;
    d->pcy = in->camera_y;
    if (iabs(dx) > SHAKE_MAX_STEP || iabs(dy) > SHAKE_MAX_STEP) {
        memset(d->dx, 0, sizeof d->dx);   /* a cut: start over */
        memset(d->dy, 0, sizeof d->dy);
        dx = dy = 0;
    }
    memmove(d->dx, d->dx + 1, sizeof d->dx - sizeof *d->dx);
    memmove(d->dy, d->dy + 1, sizeof d->dy - sizeof *d->dy);
    d->dx[RUMBLE_HISTORY - 1] = (int16_t)dx;
    d->dy[RUMBLE_HISTORY - 1] = (int16_t)dy;
    if (d->shake_cool)
        d->shake_cool--;
    if (d->n >= SHAKE_WINDOW && !d->shake_cool) {
        int tx, ax, nx, ty, ay, ny;
        oscillation(d->dx, SHAKE_WINDOW, &tx, &ax, &nx);
        oscillation(d->dy, SHAKE_WINDOW, &ty, &ay, &ny);
        int sx = tx >= SHAKE_TURNS && nx <= 2 * ax, sy = ty >= SHAKE_TURNS && ny <= 2 * ay;
        if (sx || sy) {
            int amp = (sx && ax > (sy ? ay : 0)) ? ax : ay;
            float s = 7000.0f + 3500.0f * (float)amp;
            n = add(out, n, max, frame, RUMBLE_SRC_SHAKE, "shake", clamp16(s > 36000 ? 36000 : s),
                    clamp16(s / 3), 120);
            d->shake_cool = SHAKE_REPEAT;
        }
    }
    /* flash */
    float level = (float)in->brightness / 15.0f * (float)(in->luma + in->fixed_add) / 1023.0f;
    float base = 2.0f;
    int lit = d->n >= 4;
    for (int k = RUMBLE_HISTORY - 4; k < RUMBLE_HISTORY; k++) {
        if (d->level[k] < base)
            base = d->level[k];
        lit &= d->bright[k] >= FLASH_LIT;
    }
    if (d->flash_cool)
        d->flash_cool--;
    if (lit && !d->flash_cool && level - base >= FLASH_JUMP) {
        float j = level - base;
        float s = 12000.0f + 30000.0f * j;
        n = add(out, n, max, frame, RUMBLE_SRC_FLASH, "flash", clamp16(s > 40000 ? 40000 : s),
                clamp16(s * 0.6f), 140);
        d->flash_cool = FLASH_COOL;
    }
    memmove(d->level, d->level + 1, sizeof d->level - sizeof *d->level);
    memmove(d->bright, d->bright + 1, sizeof d->bright - sizeof *d->bright);
    d->level[RUMBLE_HISTORY - 1] = level;
    d->bright[RUMBLE_HISTORY - 1] = in->brightness;
    /* sound */
    if (d->sound_cool)
        d->sound_cool--;
    if (d->sound && in->has_sound) {
        float b = in->bass;
        if (d->n >= 30 && !d->sound_cool && b > SOUND_FLOOR && b > SOUND_RATIO * d->bass_avg) {
            float s = 60000.0f * b;
            n = add(out, n, max, frame, RUMBLE_SRC_SOUND, "boom",
                    clamp16(s < 9000 ? 9000 : s > 36000 ? 36000 : s), 0, 100);
            d->sound_cool = SOUND_COOL;
        }
        d->bass_avg += (b - d->bass_avg) * (d->n < 30 ? 0.2f : 0.03f);
    }
    if (d->n < 1000)
        d->n++;
    return n;
}

void rumble_sound_feed(rumble_detect *d, const int16_t *s, int frames, int rate)
{
    if (!d->sound || frames <= 0 || rate <= 0)
        return;
    /* Two one-pole low-passes at about 120 Hz. */
    float a = 1.0f - expf(-2.0f * 3.14159265f * 120.0f / (float)rate);
    double acc = 0;
    for (int k = 0; k < frames; k++) {
        float x = ((float)s[2 * k] + (float)s[2 * k + 1]) * (0.5f / 32768.0f);
        d->lp1 += a * (x - d->lp1);
        d->lp2 += a * (d->lp1 - d->lp2);
        acc += (double)d->lp2 * d->lp2;
    }
    d->acc += acc;
    d->acc_n += frames;
}

static uint16_t luma555(uint16_t c)
{
    unsigned r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
    return (uint16_t)((2 * r + 5 * g + b) * 1023 / (8 * 31));
}

void rumble_capture(rumble_frame_in *in)
{
    const Ppu *p = snes_hw_ppu();
    memset(in, 0, sizeof *in);
    for (int k = 0; k < 3; k++) {
        in->hofs[k] = p->bgLayer[k].hScroll;
        in->vofs[k] = p->bgLayer[k].vScroll;
    }
    in->brightness = p->forcedBlank ? 0 : p->brightness;
    unsigned sum = 0;
    for (int k = 0; k < 256; k++)
        sum += luma555(p->cgram[k]);
    in->luma = (uint16_t)(sum / 256);
    /* The fixed colour added over the picture: colour math on (any layer
       or the backdrop), adding, with the fixed colour rather than the
       sub screen. */
    if (p->mathEnabled && !p->subtractColor && !p->addSubscreen) {
        uint16_t c = (uint16_t)((p->fixedColorR & 31) | (p->fixedColorG & 31) << 5 |
                                (p->fixedColorB & 31) << 10);
        in->fixed_add = luma555(c);
    }
}

static rumble_detect det;
static int det_on = 1;

void rumble_set_detectors(int on, int sound)
{
    det_on = on != 0;
    det.sound = sound != 0;
}

void rumble_frame(long frame, const int16_t *stereo, int frames, int rate)
{
    if (!enabled) {
        cur_frame = frame + 1;
        return;
    }
    cur_frame = frame;   /* the hook's events belong to this frame */
    int ctx = hook ? hook(frame) : RUMBLE_CTX_UNKNOWN;
    if (det_on) {
        rumble_frame_in in;
        rumble_capture(&in);
        if (stereo && frames > 0) {
            rumble_sound_feed(&det, stereo, frames, rate);
            if (det.acc_n) {
                in.has_sound = 1;
                in.bass = (float)sqrt(det.acc / det.acc_n);
            }
        }
        det.acc = 0;
        det.acc_n = 0;
        rumble_ev ev[4];
        int n = rumble_detect_step(&det, &in, ctx, frame, ev, 4);
        for (int k = 0; k < n; k++)
            push(&ev[k]);
    }
    cur_frame = frame + 1;   /* the patches' events, until the next edge */
}
