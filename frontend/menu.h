/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the in-game settings menu (docs/USER-GUIDE.md, "The menu";
 * docs/HANDHELD.md). Dear ImGui (third_party/imgui) draws it over the
 * picture, in either path: over the HD-2D renderer's frame through ImGui's
 * OpenGL 3 / ES 3 renderer, or over the plain picture through its
 * SDL_Renderer one. What it shows comes from the settings registry
 * (settings.h): a tab per category, a row per item.
 *
 * Navigation is the menu's own, made for a controller: d-pad or left
 * stick up / down picks a row (held: repeats), left / right changes its
 * value, A runs or toggles it, B goes back (closes the menu), L1 / R1
 * switch tabs, Start closes. Keyboard: arrows, Enter or Space, Escape or
 * Backspace, Q / W or Page Up / Page Down for the tabs (L and R in the
 * game), the menu key (F10) closes. The mouse and touch pick and change
 * rows too.
 *
 * The caller pauses the game while menu_is_open(): it calls menu_event
 * with each SDL event, and draws the frame and menu_draw each tick. */
#ifndef CT_MENU_H
#define CT_MENU_H

#include <stdint.h>

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;    /* the plain picture's renderer; NULL: the GL path */
    int gles;             /* GL path: the context is OpenGL ES 3.0 */
    const char *app;      /* "CT Recomp" */
    const char *version;  /* "0.1.0 (abc1234)" */
} menu_host;

/* Once the window (and renderer or GL context) exists. Nothing is set up
   until the menu first opens. */
void menu_init(const menu_host *h);
/* Before the window, renderer or GL context go. */
void menu_shutdown(void);

void menu_open(void);
void menu_close(void);
int menu_is_open(void);
/* Opening counted from the start (for tests and logs). */
long menu_opened_count(void);

/* An SDL event, while the menu is open. 1: the menu used it (it is not
   the game's); window, quit and controller hot-plug events are never
   used, the caller handles them as usual. */
int menu_event(const SDL_Event *ev);
/* Once per tick while open: held-direction repeats; ms since the last. */
void menu_tick(unsigned ms);
/* Draw the menu over what is in the window's back buffer (or the SDL
   renderer's target) at w x h pixels; the caller presents. */
void menu_draw(int w, int h);
/* Read back what is in the back buffer (after menu_draw, before the
   present) and write it to path as PNG. 0 on success. */
int menu_shot(const char *path, int w, int h);

/* The key the menu is waiting for (a key binding row was picked): the
   caller must not treat key presses as anything else meanwhile. */
int menu_capturing_key(void);

#ifdef __cplusplus
}
#endif

#endif
