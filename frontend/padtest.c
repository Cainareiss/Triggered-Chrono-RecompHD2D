/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the controller test (padtest.h). */
#include "padtest.h"

#include <stdio.h>
#include <stdlib.h>

#include "menu.h"
#include "rumble_out.h"   /* the rumble step */

enum {
    T_A, T_B, T_X, T_Y, T_UP, T_DOWN, T_LEFT, T_RIGHT, T_STICK, T_L1, T_R1, T_L2, T_R2,
    T_VIEW, T_MENU, T_BACK_L, T_BACK_R, T_CHORD,
    T_RUMBLE,   /* last: once the rest are done, a pulse, and A or B says whether it was felt */
    T_COUNT
};

/* Labels name the controller's buttons by position: south A, east B
   (Xbox / Steam Deck style). */
static const char *const labels[T_COUNT] = {
    [T_A] = "A (bottom face button)",
    [T_B] = "B (right face button)",
    [T_X] = "X (left face button)",
    [T_Y] = "Y (top face button)",
    [T_UP] = "D-pad up",
    [T_DOWN] = "D-pad down",
    [T_LEFT] = "D-pad left",
    [T_RIGHT] = "D-pad right",
    [T_STICK] = "Left stick: push it any way",
    [T_L1] = "L1 (left shoulder)",
    [T_R1] = "R1 (right shoulder)",
    [T_L2] = "L2: pull fully",
    [T_R2] = "R2: pull fully",
    [T_VIEW] = "View (two squares)",
    [T_MENU] = "Menu (three lines)",
    [T_BACK_L] = "Left back button: quick save",
    [T_BACK_R] = "Right back button: quick load",
    [T_CHORD] = "Click both sticks together: menu, then B",
    [T_RUMBLE] = "Rumble: did you feel it? A = yes, B = no",
};

#define RUMBLE_EVERY_MS 2000   /* the pulse again until it is answered */

static struct {
    int on;
    unsigned char done[T_COUNT];
    long menu_opens0;
    int menu_known;
    Uint32 all_done_at;
    Uint32 pulsed_at;   /* the rumble step's last pulse (0: none yet) */
    int pulses;
} P;

/* Every step before the rumble one is done: it is the one asked. */
static int rumble_asked(void)
{
    for (int k = 0; k < T_RUMBLE; k++)
        if (!P.done[k])
            return 0;
    return !P.done[T_RUMBLE];
}

static void rumble_answer(int felt)
{
    if (!P.pulses)
        return;   /* no pulse yet: nothing to answer */
    fprintf(stderr, "ct_sdl: pad test: rumble: %s (after %d pulse%s)\n",
            felt ? "felt it (A: yes)" : "not felt (B: no)", P.pulses, P.pulses == 1 ? "" : "s");
    P.done[T_RUMBLE] = 1;
    rumble_out_stop_all();
    int left = 0;
    for (int k = 0; k < T_COUNT; k++)
        left += !P.done[k];
    fprintf(stderr, "ct_sdl: pad test: ok %s (%d left)\n", labels[T_RUMBLE], left);
}

static void tick_off(int t)
{
    if (P.done[t])
        return;
    P.done[t] = 1;
    int left = 0;
    for (int k = 0; k < T_COUNT; k++)
        left += !P.done[k];
    fprintf(stderr, "ct_sdl: pad test: ok %s (%d left)\n", labels[t], left);
}

void padtest_start(void)
{
    P.on = 1;
    fprintf(stderr, "ct_sdl: pad test: %d controls to check; the list is on screen\n", T_COUNT);
}

int padtest_active(void) { return P.on; }

void padtest_event(const SDL_Event *ev)
{
    if (!P.on)
        return;
    if (ev->type == SDL_CONTROLLERBUTTONDOWN && rumble_asked() &&
        (ev->cbutton.button == SDL_CONTROLLER_BUTTON_A ||
         ev->cbutton.button == SDL_CONTROLLER_BUTTON_B)) {
        rumble_answer(ev->cbutton.button == SDL_CONTROLLER_BUTTON_A);
        return;
    }
    if (ev->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: tick_off(T_A); break;
        case SDL_CONTROLLER_BUTTON_B: tick_off(T_B); break;
        case SDL_CONTROLLER_BUTTON_X: tick_off(T_X); break;
        case SDL_CONTROLLER_BUTTON_Y: tick_off(T_Y); break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: tick_off(T_UP); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: tick_off(T_DOWN); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: tick_off(T_LEFT); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: tick_off(T_RIGHT); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: tick_off(T_L1); break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: tick_off(T_R1); break;
        case SDL_CONTROLLER_BUTTON_BACK: tick_off(T_VIEW); break;
        case SDL_CONTROLLER_BUTTON_START: tick_off(T_MENU); break;
        /* SDL's Deck driver: paddle2 = L4, paddle4 = L5 (left), paddle1 = R4,
           paddle3 = R5 (right). Either one on a side counts. */
        case SDL_CONTROLLER_BUTTON_PADDLE2:
        case SDL_CONTROLLER_BUTTON_PADDLE4: tick_off(T_BACK_L); break;
        case SDL_CONTROLLER_BUTTON_PADDLE1:
        case SDL_CONTROLLER_BUTTON_PADDLE3: tick_off(T_BACK_R); break;
        default: break;
        }
    } else if (ev->type == SDL_CONTROLLERAXISMOTION) {
        int v = ev->caxis.value;
        switch (ev->caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
        case SDL_CONTROLLER_AXIS_LEFTY:
            if (abs(v) > 20000)
                tick_off(T_STICK);
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            if (v > 24000)
                tick_off(T_L2);
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            if (v > 24000)
                tick_off(T_R2);
            break;
        default: break;
        }
    }
}

int padtest_tick(long menu_opens)
{
    if (!P.on)
        return 0;
    if (!P.menu_known) {
        P.menu_known = 1;
        P.menu_opens0 = menu_opens;
    } else if (menu_opens > P.menu_opens0) {
        tick_off(T_CHORD);
    }
    if (rumble_asked()) {   /* a pulse now, and again every 2 s until answered */
        Uint32 now = SDL_GetTicks();
        if (!P.pulsed_at || now - P.pulsed_at >= RUMBLE_EVERY_MS) {
            if (!P.pulses)
                fprintf(stderr, "ct_sdl: pad test: rumble: a pulse every 2 s; A if you feel it, "
                                "B if not\n");
            rumble_out_test(now);
            P.pulsed_at = now | 1;
            P.pulses++;
        }
    }
    int all = 1;
    for (int k = 0; k < T_COUNT; k++)
        all &= P.done[k];
    if (all && !P.all_done_at) {
        P.all_done_at = SDL_GetTicks() | 1;
        fprintf(stderr, "ct_sdl: pad test: every control works\n");
    }
    return all && SDL_GetTicks() - P.all_done_at > 5000;
}

void padtest_draw(int w, int h)
{
    if (!P.on)
        return;
    int current = -1;
    for (int k = 0; k < T_COUNT && current < 0; k++)
        if (!P.done[k])
            current = k;
    const char *footer = current < 0          ? "All done! Closing in a few seconds."
                         : current == T_RUMBLE ? "A pulse every 2 seconds: A if you feel it, B if not."
                                               : "Press each control once, in any order.";
    menu_draw_checklist(w, h, "Controller test", T_COUNT, labels, P.done, current, footer);
}
