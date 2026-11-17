/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: which controller buttons and keys emit which frontend action
 * (actions.h). Only buttons the SNES pad doesn't use can be bound: the
 * back buttons / grips (SDL's paddle1-4), misc1 (the Deck's ... button,
 * a Switch Pro's capture button) and the touchpad click.
 *
 * SDL's paddles on a Steam Deck (SDL_hidapi_steamdeck.c): paddle1 = R4,
 * paddle2 = L4, paddle3 = R5, paddle4 = L5; on an Xbox Elite pad P1-P4,
 * on the first Steam Controller the right / left grip (paddle1 / 2).
 *
 * Defaults (docs/HANDHELD.md):
 *   L4 (paddle2) quick save     R4 (paddle1) quick load
 *   L5 (paddle4) fast-forward   R5 (paddle3) menu
 *   misc1 (the Deck's "..." button) quick menu
 *   F6 quick save, F9 quick load, Tab fast-forward (hold), F10 menu,
 *   F8 screenshot, F1 quick menu.
 * F2-F4 and F11 are the renderer's and co-op's, F5 the asset packs'
 * reload, F12 Steam's screenshot: none of them is a default here, and
 * only the quick menu action may have F1.
 *
 * settings.ini: bind_paddle1=quickload ... (bind_<SDL button name>=
 * <action name>), key_quicksave=F6 ... (key_<action>=<SDL key name>,
 * "none" for no key). */
#ifndef CT_BINDINGS_H
#define CT_BINDINGS_H

#include <stdio.h>

#include <SDL.h>

#include "actions.h"

typedef struct {
    unsigned char button[SDL_CONTROLLER_BUTTON_MAX];   /* ct_action per button */
    SDL_Scancode key[ACTION_COUNT];                    /* SDL_SCANCODE_UNKNOWN: none */
} bindings;

void bindings_defaults(bindings *b);
/* Can this button carry an action (not an SNES button, not R3 / L3)? */
int bindings_bindable(SDL_GameControllerButton button);
/* One settings.ini line ("bind_...=" / "key_...="): 1 if it was one of
   ours (applied when valid, else a warning on stderr), 0 if not. */
int bindings_parse_line(bindings *b, const char *line);
/* All of them, as settings.ini lines. */
void bindings_write(const bindings *b, FILE *f);

ct_action bindings_for_button(const bindings *b, SDL_GameControllerButton button);
ct_action bindings_for_key(const bindings *b, SDL_Scancode key);
/* The first button that emits a, or SDL_CONTROLLER_BUTTON_INVALID. */
SDL_GameControllerButton bindings_button_of(const bindings *b, ct_action a);

/* The frontend's bindings (settings.ini), for the glyphs (glyphs.h). */
bindings *bindings_current(void);

/* Per controller: the bound buttons' state last frame, so a press emits
   one action press and a release one release. */
typedef struct {
    unsigned char down[SDL_CONTROLLER_BUTTON_MAX];
} binder;

void binder_init(binder *s);
/* Once per frame with the controller's buttons (button(ctx, b) as in
   input.h). Returns how many actions it emitted. */
int binder_update(binder *s, const bindings *b,
                  int (*button)(void *ctx, SDL_GameControllerButton b), void *ctx);
/* The controller went away: release what it held. */
void binder_release(binder *s, const bindings *b);

/* Keyboard: a key event. Returns 1 if the key is bound (then it is not
   game input), 0 if not. Repeats are ignored but still claimed. */
int bindings_key_event(const bindings *b, SDL_Scancode key, int down, int repeat);
/* Let go of the held keys' actions (the window lost focus). */
void bindings_keys_release(void);

#endif
