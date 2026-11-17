/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the F1 quick menu (docs/USER-GUIDE.md, "Quick menu"; docs/HANDHELD.md):
 * a bottom sheet of big tiles over the paused picture, made for a
 * controller and a finger: Game (resume, quick save / load, slots, volume),
 * Look, and "All settings..." which opens the F10 menu (menu.h). One
 * level, no tabs; the sections with no tile yet are not drawn.
 *
 * Navigation: the d-pad or left stick moves a focus ring over the grid
 * (held: repeats), A runs a tile, left / right adjust a slider or a
 * stepper (at its end they move on), L1 / R1 jump a section, B, Start and
 * the quick menu action (F1, misc1) close. Keyboard: arrows, Enter or
 * Space, Escape or Backspace, Q / W or Page Up / Page Down. Mouse and
 * touch: tap a tile, drag a slider, drag the sheet to scroll, tap above
 * it to close. The hint line shows the last-used device's buttons.
 *
 * Same shape as menu.h: the caller pauses the game while
 * quickmenu_is_open() (main.c menu_run), feeds quickmenu_event with each
 * SDL event, ticks it and draws it after the picture. It shares the ImGui
 * backend (ui_backend.h) the F10 menu sets up (menu_init gives the host). */
#ifndef CT_QUICKMENU_H
#define CT_QUICKMENU_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void quickmenu_open(void);
void quickmenu_close(void);
int quickmenu_is_open(void);
/* Opening counted from the start (for tests and logs). */
long quickmenu_opened_count(void);

/* An SDL event, while open. 1: used (it is not the game's); window, quit
   and controller hot-plug events and unknown keys are not (the caller
   handles them; the quick menu key closes it, in main.c). */
int quickmenu_event(const SDL_Event *ev);
/* Once per tick while open: held-direction repeats, the slide, messages;
   ms since the last tick. */
void quickmenu_tick(unsigned ms);
/* Draw over what is in the window's back buffer (or the SDL renderer's
   target) at w x h pixels; the caller presents. */
void quickmenu_draw(int w, int h);

/* What the frontend gives the tiles that are not settings rows: cycling
   the HD-2D look (it needs the renderer), and the slot save / load (qol.h). NULL members: that tile is off. */
typedef struct {
    void (*look_cycle)(void);
    int (*look_enabled)(void);
    void (*save_slot)(int slot);   /* slots 1-4 (qol_save_slot) */
    void (*load_slot)(int slot);
} quickmenu_hooks;
void quickmenu_set_hooks(const quickmenu_hooks *h);

/* For tests: the tiles drawn now (after a quickmenu_draw), in order. */
int quickmenu_tile_count(void);
const char *quickmenu_tile_title(int n);
int quickmenu_columns(void);
/* The tile's rectangle in window pixels (0 ok, -1 out of view or range). */
int quickmenu_tile_rect(int n, int *x, int *y, int *w, int *h);
/* The focused tile (an index of the tiles above), -1 none. */
int quickmenu_focus(void);
/* The tile's value as drawn ("On", "Slot 2", "80%"), into buf. */
const char *quickmenu_tile_value(int n, char *buf, unsigned size);

#ifdef __cplusplus
}
#endif

#endif
