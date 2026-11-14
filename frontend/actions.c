/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: frontend actions (actions.h). */
#include "actions.h"

#include <stdio.h>
#include <string.h>
#include <SDL.h>

static struct {
    action_fn fn;
    void *user;
    int held;
} act[ACTION_COUNT];
static void (*message)(const char *text);
static void (*trace)(ct_action a, int down);

static const char *const names[ACTION_COUNT] = {"none", "quicksave", "quickload", "ffwd", "menu",
                                                "screenshot"};
static const char *const titles[ACTION_COUNT] = {"Nothing", "Quick save", "Quick load",
                                                 "Fast-forward", "Menu", "Screenshot"};

static int valid(ct_action a)
{
    return a > ACTION_NONE && a < ACTION_COUNT;
}

static void stub(ct_action a, int down)
{
    if (!down)
        return;
    char msg[96];
    snprintf(msg, sizeof msg, "%s: not available yet", titles[a]);
    if (message)
        message(msg);
    else
        fprintf(stderr, "ct: %s\n", msg);
}

void actions_set_handler(ct_action a, action_fn fn, void *user)
{
    if (!valid(a))
        return;
    act[a].fn = fn;
    act[a].user = user;
}

void actions_set_message(void (*msg)(const char *text))
{
    message = msg;
}

void actions_set_trace(void (*fn)(ct_action a, int down))
{
    trace = fn;
}

static void run(ct_action a, int down)
{
    if (trace)
        trace(a, down);
    if (act[a].fn)
        act[a].fn(a, down, act[a].user);
    else
        stub(a, down);
}

void actions_emit(ct_action a, int down)
{
    if (!valid(a))
        return;
    if (!actions_is_hold(a)) {
        if (down)
            run(a, 1);
        return;
    }
    if (down) {
        if (act[a].held++ == 0)
            run(a, 1);
    } else if (act[a].held > 0 && --act[a].held == 0) {
        run(a, 0);
    }
}

int actions_held(ct_action a)
{
    return valid(a) ? act[a].held : 0;
}

void actions_release_all(void)
{
    for (int a = 1; a < ACTION_COUNT; a++)
        if (act[a].held) {
            act[a].held = 0;
            run((ct_action)a, 0);
        }
}

int actions_is_hold(ct_action a)
{
    return a == ACTION_FFWD;
}

const char *action_name(ct_action a)
{
    return (int)a >= 0 && a < ACTION_COUNT ? names[a] : "none";
}

const char *action_title(ct_action a)
{
    return (int)a >= 0 && a < ACTION_COUNT ? titles[a] : "";
}

int action_parse(const char *name)
{
    for (int a = 0; a < ACTION_COUNT; a++)
        if (!SDL_strcasecmp(name, names[a]))
            return a;
    return -1;
}
