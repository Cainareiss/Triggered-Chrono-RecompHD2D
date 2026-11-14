/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: playing simulated rumble (runtime/rumble.h) on the players'
 * controllers (docs/USER-GUIDE.md, "Rumble"). Each frame the events the
 * game and the detectors raised are taken from the queue and become
 * pulses on the pad of the player they are for (every pad in play for
 * RUMBLE_ALL). A pulse holds its strength briefly, then fades over its
 * length; several at once take the strongest. The motors are set with
 * SDL_GameControllerRumble (and the motors behind L2 and R2, where the pad has
 * them), resent while a pulse lasts, each time with a short timeout so
 * that nothing keeps shaking if the program stalls.
 *
 * settings.ini rumble=off|light|medium|strong (default light) scales
 * every pulse. While the game is held (paused, fast-forward, the menu)
 * events are dropped and the motors stopped; also on quit and when a
 * controller goes away. */
#ifndef CT_RUMBLE_OUT_H
#define CT_RUMBLE_OUT_H

#include <stdint.h>

#include <SDL.h>

#include "rumble.h"

enum { RUMBLE_OFF, RUMBLE_LIGHT, RUMBLE_MEDIUM, RUMBLE_STRONG, RUMBLE_LEVELS };

#define RUMBLE_PADS 4
#define RUMBLE_PULSES 8

/* settings.ini values, menu labels. */
const char *rumble_level_value(int level);   /* "off", "light", ... */
const char *rumble_level_label(int level);   /* "Off", "Light", ... */
void rumble_out_set_level(int level);        /* also rumble_set_enabled */
int rumble_out_level(void);
/* --log-rumble: each event taken and each motor change, on stderr. */
void rumble_out_set_log(int on);

/* The pads in play this frame: pads[p] is player p's (NULL: none). */
void rumble_out_set_pads(SDL_GameController *const pads[RUMBLE_PADS]);

/* At the frame edge: the events raised since the last call become pulses,
   unless held (then they are dropped and every motor stops). now: ms. */
void rumble_out_frame(uint32_t now, int held);
/* Between frames (pause, the menu): keeps a test pulse going, and the
   rest stopped. */
void rumble_out_idle(uint32_t now);
/* A test pulse on every pad in play (the menu's "Test rumble", the
   controller test), at the level set, or at light when it is off.
   Plays even while held. */
void rumble_out_test(uint32_t now);
/* Every motor off now, every pulse dropped (quit, a pad removed). */
void rumble_out_stop_all(void);

/* ---- the mixer, for tests ---- */
typedef struct {
    uint32_t start, ms;
    uint16_t low, high;
} rumble_pulse;

/* The motors' strengths at now from n pulses (expired ones ignored). */
void rumble_mix(const rumble_pulse *p, int n, uint32_t now, uint16_t *low, uint16_t *high);
/* A level's scale, in percent (0 for off). */
int rumble_level_scale(int level);

#endif
