/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: action bindings (bindings.h). */
#include "bindings.h"

#include <string.h>

static const SDL_GameControllerButton bindable[] = {
    SDL_CONTROLLER_BUTTON_PADDLE1, SDL_CONTROLLER_BUTTON_PADDLE2, SDL_CONTROLLER_BUTTON_PADDLE3,
    SDL_CONTROLLER_BUTTON_PADDLE4, SDL_CONTROLLER_BUTTON_MISC1,   SDL_CONTROLLER_BUTTON_TOUCHPAD,
};
#define N_BINDABLE (int)(sizeof bindable / sizeof *bindable)

void bindings_defaults(bindings *b)
{
    memset(b, 0, sizeof *b);
    b->button[SDL_CONTROLLER_BUTTON_PADDLE2] = ACTION_QUICKSAVE;   /* L4 */
    b->button[SDL_CONTROLLER_BUTTON_PADDLE1] = ACTION_QUICKLOAD;   /* R4 */
    b->button[SDL_CONTROLLER_BUTTON_PADDLE4] = ACTION_FFWD;        /* L5 */
    b->button[SDL_CONTROLLER_BUTTON_PADDLE3] = ACTION_MENU;        /* R5 */
    b->button[SDL_CONTROLLER_BUTTON_MISC1] = ACTION_QUICKMENU;     /* the Deck's "..." button */
    for (int a = 0; a < ACTION_COUNT; a++)
        b->key[a] = SDL_SCANCODE_UNKNOWN;
    b->key[ACTION_QUICKSAVE] = SDL_SCANCODE_F6;
    b->key[ACTION_QUICKLOAD] = SDL_SCANCODE_F9;
    b->key[ACTION_FFWD] = SDL_SCANCODE_TAB;
    b->key[ACTION_MENU] = SDL_SCANCODE_F10;
    b->key[ACTION_SCREENSHOT] = SDL_SCANCODE_F8;
    b->key[ACTION_QUICKMENU] = SDL_SCANCODE_F1;
}

int bindings_bindable(SDL_GameControllerButton button)
{
    for (int k = 0; k < N_BINDABLE; k++)
        if (bindable[k] == button)
            return 1;
    return 0;
}

/* Keys the frontend already uses, or the game: never bound. F1 is the
   quick menu's own (it used to cycle the look, which is a tile of the quick
   menu now): only that action may have it. */
static int key_taken(SDL_Scancode k, ct_action a)
{
    static const SDL_Scancode taken[] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_Q,
        SDL_SCANCODE_W, SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F11,
    };
    if (k == SDL_SCANCODE_F1)
        return a != ACTION_QUICKMENU;
    for (size_t n = 0; n < sizeof taken / sizeof *taken; n++)
        if (taken[n] == k)
            return 1;
    return 0;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

int bindings_parse_line(bindings *b, const char *line)
{
    char key[64], val[64];
    const char *eq = strchr(line, '=');
    int is_bind = !strncmp(line, "bind_", 5), is_key = !strncmp(line, "key_", 4);
    if ((!is_bind && !is_key) || !eq || eq - line >= (long)sizeof key)
        return 0;
    memcpy(key, line, (size_t)(eq - line));
    key[eq - line] = 0;
    snprintf(val, sizeof val, "%s", eq + 1);
    trim(val);
    if (is_bind) {
        SDL_GameControllerButton btn = SDL_GameControllerGetButtonFromString(key + 5);
        int a = action_parse(val);
        if (!bindings_bindable(btn) || a < 0) {
            fprintf(stderr, "ct: settings.ini: %s=%s: %s\n", key, val,
                    !bindings_bindable(btn) ? "not a button that can be bound (paddle1-4, misc1, "
                                              "touchpad)"
                                            : "not an action (none, quicksave, quickload, ffwd, "
                                              "menu, screenshot, quickmenu)");
            return 1;
        }
        b->button[btn] = (unsigned char)a;
        return 1;
    }
    int a = action_parse(key + 4);
    if (a <= ACTION_NONE) {
        fprintf(stderr, "ct: settings.ini: %s: not an action\n", key);
        return 1;
    }
    if (!SDL_strcasecmp(val, "none") || !val[0]) {
        b->key[a] = SDL_SCANCODE_UNKNOWN;
        return 1;
    }
    SDL_Scancode sc = SDL_GetScancodeFromName(val);
    if (sc == SDL_SCANCODE_UNKNOWN || key_taken(sc, (ct_action)a)) {
        fprintf(stderr, "ct: settings.ini: %s=%s: %s\n", key, val,
                sc == SDL_SCANCODE_UNKNOWN ? "not a key name" : "that key is already used");
        return 1;
    }
    for (int o = 1; o < ACTION_COUNT; o++)   /* one action per key */
        if (o != a && b->key[o] == sc)
            b->key[o] = SDL_SCANCODE_UNKNOWN;
    b->key[a] = sc;
    return 1;
}

void bindings_write(const bindings *b, FILE *f)
{
    for (int k = 0; k < N_BINDABLE; k++)
        fprintf(f, "bind_%s=%s\n", SDL_GameControllerGetStringForButton(bindable[k]),
                action_name((ct_action)b->button[bindable[k]]));
    for (int a = 1; a < ACTION_COUNT; a++)
        fprintf(f, "key_%s=%s\n", action_name((ct_action)a),
                b->key[a] == SDL_SCANCODE_UNKNOWN ? "none" : SDL_GetScancodeName(b->key[a]));
}

ct_action bindings_for_button(const bindings *b, SDL_GameControllerButton button)
{
    if ((int)button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
        return ACTION_NONE;
    return (ct_action)b->button[button];
}

ct_action bindings_for_key(const bindings *b, SDL_Scancode key)
{
    if (key == SDL_SCANCODE_UNKNOWN)
        return ACTION_NONE;
    for (int a = 1; a < ACTION_COUNT; a++)
        if (b->key[a] == key)
            return (ct_action)a;
    return ACTION_NONE;
}

SDL_GameControllerButton bindings_button_of(const bindings *b, ct_action a)
{
    for (int k = 0; k < N_BINDABLE; k++)
        if (b->button[bindable[k]] == a && a != ACTION_NONE)
            return bindable[k];
    return SDL_CONTROLLER_BUTTON_INVALID;
}

bindings *bindings_current(void)
{
    static bindings cur;
    static int init;
    if (!init) {
        init = 1;
        bindings_defaults(&cur);
    }
    return &cur;
}

void binder_init(binder *s)
{
    memset(s, 0, sizeof *s);
}

int binder_update(binder *s, const bindings *b,
                  int (*button)(void *ctx, SDL_GameControllerButton b), void *ctx)
{
    int n = 0;
    for (int k = 0; k < N_BINDABLE; k++) {
        SDL_GameControllerButton btn = bindable[k];
        int d = button(ctx, btn) != 0;
        if (d == s->down[btn])
            continue;
        s->down[btn] = (unsigned char)d;
        ct_action a = bindings_for_button(b, btn);
        if (a != ACTION_NONE) {
            actions_emit(a, d);
            n++;
        }
    }
    return n;
}

void binder_release(binder *s, const bindings *b)
{
    for (int k = 0; k < N_BINDABLE; k++) {
        SDL_GameControllerButton btn = bindable[k];
        if (s->down[btn]) {
            s->down[btn] = 0;
            actions_emit(bindings_for_button(b, btn), 0);
        }
    }
}

/* Which action each held key started, so a rebind while held can't
   strand a hold. */
static ct_action key_down[SDL_NUM_SCANCODES];

int bindings_key_event(const bindings *b, SDL_Scancode key, int down, int repeat)
{
    if ((unsigned)key >= SDL_NUM_SCANCODES)
        return 0;
    if (!down) {
        ct_action a = key_down[key];
        key_down[key] = ACTION_NONE;
        if (a != ACTION_NONE)
            actions_emit(a, 0);
        return a != ACTION_NONE || bindings_for_key(b, key) != ACTION_NONE;
    }
    ct_action a = bindings_for_key(b, key);
    if (a == ACTION_NONE)
        return 0;
    if (!repeat && key_down[key] == ACTION_NONE) {
        key_down[key] = a;
        actions_emit(a, 1);
    }
    return 1;
}

void bindings_keys_release(void)
{
    for (int k = 0; k < SDL_NUM_SCANCODES; k++)
        if (key_down[k] != ACTION_NONE) {
            ct_action a = key_down[k];
            key_down[k] = ACTION_NONE;
            actions_emit(a, 0);
        }
}
