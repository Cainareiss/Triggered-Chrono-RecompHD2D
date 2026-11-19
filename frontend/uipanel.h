/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: remastered menus: the game's windows (text boxes, menus, battle
 * windows) found on the screen each frame, so the renderer can draw a
 * modern panel in their place (hd2d_set_ui_panels) under the text, the
 * icons and the cursor, which stay the console's. No game data: the
 * windows are found from their shape in the provenance G-buffer (a frame
 * of one tile per edge around a repeating background), not by tile.
 * No SDL, no GL. */
#ifndef CT_UIPANEL_H
#define CT_UIPANEL_H

#include <stdint.h>

#include "sched.h"

#define UIPANEL_MAX 16

/* One frame: prov (sched_prov, SCHED_WIDTH x SCHED_HEIGHT) and the pixels
   HD text took out (hdtext_removed, or NULL). Fills mask (W x H: 1 where
   the panel replaces the console's pixel) and up to UIPANEL_MAX window
   rectangles, console pixels x, y, w, h. Returns how many. */
int uipanel_frame(const sched_prov_px *prov, const uint8_t *text_mask, uint8_t *mask,
                  float rects[UIPANEL_MAX][4]);

#endif
