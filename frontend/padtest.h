/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the controller test (--pad-test, docs/HANDHELD.md). A checklist
 * of every control in the corner of the screen: each one is ticked off
 * the first time it is used, the next one is highlighted, and the game
 * keeps running underneath. When everything is ticked it says so and the
 * game quits a few seconds later. Each step is also logged to stderr.
 * The last step is the rumble: once every control is ticked, the pads
 * pulse every 2 s (rumble_out_test) until A (felt it) or B (didn't)
 * answers, and the answer is logged. */
#ifndef CT_PADTEST_H
#define CT_PADTEST_H

#include <SDL.h>

void padtest_start(void);
int padtest_active(void);
/* Every SDL event, before anything else sees it (nothing is used up). */
void padtest_event(const SDL_Event *ev);
/* Once per frame: menu_opens is menu_opened_count(). 1 once every step is
   done and the closing pause is over (the caller quits). */
int padtest_tick(long menu_opens);
/* Over the picture, before the present (menu_draw_checklist). */
void padtest_draw(int w, int h);

#endif
