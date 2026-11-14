/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: frontend actions: things a button or key asks the frontend (not
 * the game) to do: quick save, quick load, fast-forward, the menu, a
 * screenshot. The back buttons and grips (bindings.h) and their keyboard
 * keys emit them; whatever implements an action hooks into it with
 * actions_set_handler. An action with no handler runs a stub that says
 * on screen that it is not there yet (actions_set_message).
 *
 * Tap actions run on each press. Hold actions (actions_is_hold: fast-
 * forward) get a press when the first source starts holding and a release
 * when the last lets go, so a key and a back button held together don't
 * end it early. */
#ifndef CT_ACTIONS_H
#define CT_ACTIONS_H

typedef enum {
    ACTION_NONE,
    ACTION_QUICKSAVE,
    ACTION_QUICKLOAD,
    ACTION_FFWD,         /* hold */
    ACTION_MENU,
    ACTION_SCREENSHOT,
    ACTION_COUNT
} ct_action;

/* down: 1 on a press, 0 on a release (hold actions only). */
typedef void (*action_fn)(ct_action a, int down, void *user);

/* fn NULL puts the stub back. */
void actions_set_handler(ct_action a, action_fn fn, void *user);
/* Where the stubs' messages go (an on-screen message); NULL: stderr. */
void actions_set_message(void (*msg)(const char *text));
/* Called with every press and release given to a handler (--log-input). */
void actions_set_trace(void (*trace)(ct_action a, int down));

/* One source's press or release. Releases of a source that wasn't down
   are the caller's to avoid (bindings.h's binder does). */
void actions_emit(ct_action a, int down);
/* Sources holding a (hold actions), 0 when none. */
int actions_held(ct_action a);
/* Let go of everything (focus lost, a controller gone). */
void actions_release_all(void);

int actions_is_hold(ct_action a);
/* settings.ini names: none, quicksave, quickload, ffwd, menu, screenshot. */
const char *action_name(ct_action a);
/* For people: "Quick save", "Fast-forward", ... */
const char *action_title(ct_action a);
/* A name (action_name, any case) to its action; -1 if it is none of them. */
int action_parse(const char *name);

#endif
