/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: simulated rumble on the controllers (rumble_out.h). */
#include "rumble_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESEND_MS 120    /* while a pulse lasts, the motors are set this often */
#define TIMEOUT_MS 200   /* each setting's own length: a stall stops them */
#define HOLD_MS 40       /* a pulse's full-strength start, at most */
#define CHANGE 1200      /* a smaller change waits for the next resend */

static const char *const values[RUMBLE_LEVELS] = {"off", "light", "medium", "strong"};
static const char *const labels[RUMBLE_LEVELS] = {"Off", "Light", "Medium", "Strong"};
static const int scales[RUMBLE_LEVELS] = {0, 45, 70, 100};

const char *rumble_level_value(int l) { return l >= 0 && l < RUMBLE_LEVELS ? values[l] : "?"; }
const char *rumble_level_label(int l) { return l >= 0 && l < RUMBLE_LEVELS ? labels[l] : "?"; }
int rumble_level_scale(int l) { return l >= 0 && l < RUMBLE_LEVELS ? scales[l] : 0; }

static int level = RUMBLE_LIGHT, log_on;

static struct {
    SDL_GameController *pad;
    rumble_pulse pulse[RUMBLE_PULSES];
    int n;
    uint32_t test_until;
    uint16_t low, high;   /* last sent */
    uint32_t sent;        /* when */
    int on;               /* the motors may be running */
} slot[RUMBLE_PADS];

void rumble_out_set_level(int l)
{
    if (l < 0 || l >= RUMBLE_LEVELS)
        return;
    level = l;
    rumble_set_enabled(l != RUMBLE_OFF);
    if (l == RUMBLE_OFF)
        rumble_out_stop_all();
}

int rumble_out_level(void) { return level; }
void rumble_out_set_log(int on) { log_on = on; }

void rumble_mix(const rumble_pulse *p, int n, uint32_t now, uint16_t *low, uint16_t *high)
{
    unsigned lo = 0, hi = 0;
    for (int k = 0; k < n; k++) {
        uint32_t t = now - p[k].start;
        if ((int32_t)t < 0 || t >= p[k].ms)
            continue;
        uint32_t hold = p[k].ms / 3 < HOLD_MS ? p[k].ms / 3 : HOLD_MS;
        /* full strength, then a straight fade to a quarter at the end */
        unsigned env = t < hold ? 1024 : 1024 - 768 * (t - hold) / (p[k].ms - hold);
        unsigned l = p[k].low * env / 1024, h = p[k].high * env / 1024;
        if (l > lo)
            lo = l;
        if (h > hi)
            hi = h;
    }
    *low = (uint16_t)lo;
    *high = (uint16_t)hi;
}

static void motors(int s, uint16_t lo, uint16_t hi, uint32_t now)
{
    SDL_GameController *c = slot[s].pad;
    if (!c)
        return;
    if (!lo && !hi) {
        if (slot[s].on) {
            SDL_GameControllerRumble(c, 0, 0, 0);
            if (SDL_GameControllerHasRumbleTriggers(c))
                SDL_GameControllerRumbleTriggers(c, 0, 0, 0);
            if (log_on)
                fprintf(stderr, "ct_sdl: rumble pad %d off\n", s + 1);
        }
        slot[s].on = 0;
        slot[s].low = slot[s].high = 0;
        return;
    }
    int d = abs((int)lo - slot[s].low) > abs((int)hi - slot[s].high)
                ? abs((int)lo - slot[s].low) : abs((int)hi - slot[s].high);
    if (slot[s].on && d < CHANGE && now - slot[s].sent < RESEND_MS)
        return;
    int r = SDL_GameControllerRumble(c, lo, hi, TIMEOUT_MS);
    if (SDL_GameControllerHasRumbleTriggers(c))   /* impulse triggers: a share of the heavy motor */
        SDL_GameControllerRumbleTriggers(c, lo / 3, lo / 3, TIMEOUT_MS);
    if (log_on && (!slot[s].on || d >= CHANGE))
        fprintf(stderr, "ct_sdl: rumble pad %d motors %u %u%s\n", s + 1, lo, hi,
                r ? " (the pad has no rumble)" : "");
    slot[s].on = 1;
    slot[s].low = lo;
    slot[s].high = hi;
    slot[s].sent = now;
}

static void update(uint32_t now)
{
    for (int s = 0; s < RUMBLE_PADS; s++) {
        uint16_t lo, hi;
        rumble_mix(slot[s].pulse, slot[s].n, now, &lo, &hi);
        int k = 0;   /* forget the finished ones */
        for (int j = 0; j < slot[s].n; j++)
            if (now - slot[s].pulse[j].start < slot[s].pulse[j].ms)
                slot[s].pulse[k++] = slot[s].pulse[j];
        slot[s].n = k;
        motors(s, lo, hi, now);
    }
}

static void add(int s, uint16_t lo, uint16_t hi, uint32_t ms, uint32_t now)
{
    if (!slot[s].pad || (!lo && !hi))
        return;
    int k = slot[s].n;
    if (k == RUMBLE_PULSES) {   /* full: replace the one ending first */
        k = 0;
        for (int j = 1; j < RUMBLE_PULSES; j++)
            if (slot[s].pulse[j].start + slot[s].pulse[j].ms <
                slot[s].pulse[k].start + slot[s].pulse[k].ms)
                k = j;
    } else {
        slot[s].n++;
    }
    slot[s].pulse[k] = (rumble_pulse){now, ms, lo, hi};
}

void rumble_out_set_pads(SDL_GameController *const pads[RUMBLE_PADS])
{
    for (int s = 0; s < RUMBLE_PADS; s++)
        if (slot[s].pad != pads[s]) {
            /* another controller (or none): the old one is left as SDL
               closes it; the new one starts still */
            memset(&slot[s], 0, sizeof slot[s]);
            slot[s].pad = pads[s];
        }
}

static void drop_pulses(void)
{
    for (int s = 0; s < RUMBLE_PADS; s++)
        if (!(slot[s].test_until && (int32_t)(slot[s].test_until - SDL_GetTicks()) > 0)) {
            slot[s].n = 0;
            slot[s].test_until = 0;
        }
}

void rumble_out_frame(uint32_t now, int held)
{
    rumble_ev ev[32];
    int n, scale = rumble_level_scale(level);
    while ((n = rumble_take(ev, 32)) > 0)
        for (int k = 0; k < n && !held && scale; k++) {
            const rumble_ev *e = &ev[k];
            if (log_on)
                fprintf(stderr, "ct_sdl: rumble frame %ld player %d %s %u %u %u ms\n", e->frame,
                        e->player, e->tag, e->low, e->high, e->ms);
            uint16_t lo = (uint16_t)(e->low * scale / 100), hi = (uint16_t)(e->high * scale / 100);
            for (int s = 0; s < RUMBLE_PADS; s++)
                if (e->player == RUMBLE_ALL || e->player == s)
                    add(s, lo, hi, e->ms, now);
        }
    if (held)
        drop_pulses();
    update(now);
}

void rumble_out_idle(uint32_t now)
{
    drop_pulses();
    update(now);
}

void rumble_out_test(uint32_t now)
{
    int scale = rumble_level_scale(level ? level : RUMBLE_LIGHT);
    for (int s = 0; s < RUMBLE_PADS; s++)
        if (slot[s].pad) {
            add(s, (uint16_t)(36000 * scale / 100), (uint16_t)(30000 * scale / 100), 300, now);
            slot[s].test_until = now + 300;
        }
    update(now);
}

void rumble_out_stop_all(void)
{
    for (int s = 0; s < RUMBLE_PADS; s++) {
        slot[s].n = 0;
        slot[s].test_until = 0;
        if (slot[s].pad && slot[s].on && SDL_GameControllerGetAttached(slot[s].pad)) {
            SDL_GameControllerRumble(slot[s].pad, 0, 0, 0);
            if (SDL_GameControllerHasRumbleTriggers(slot[s].pad))
                SDL_GameControllerRumbleTriggers(slot[s].pad, 0, 0, 0);
        }
        slot[s].on = 0;
        slot[s].low = slot[s].high = 0;
    }
}
