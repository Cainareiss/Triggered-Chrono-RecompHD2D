/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The device the engine runs on, for handheld defaults (docs/HANDHELD.md):
 * whether it was started from Steam's Game Mode (gamescope), and whether
 * the hardware is a Steam Deck (its APU, "Van Gogh", gets a lighter HD-2D
 * look by default) or a ROG Ally. */
#ifndef CT_DEVICE_H
#define CT_DEVICE_H

/* Started under gamescope / Steam's gamepad UI: any of
   $GAMESCOPE_WAYLAND_DISPLAY, $SteamDeck=1, $SteamGamepadUI or
   $XDG_CURRENT_DESKTOP=gamescope. why (may be NULL) gets the variable that
   decided it. */
int device_game_mode(const char **why);

enum { DEVICE_PC, DEVICE_STEAM_DECK, DEVICE_ROG_ALLY };

/* From the firmware's product name (/sys/class/dmi/id/product_name:
   Jupiter = Steam Deck LCD, Galileo = OLED; "ROG Ally ..." for the Ally).
   $CT_DEVICE_PRODUCT, when set, replaces the file (for tests). */
int device_kind(void);
const char *device_product(void);   /* the product name read, or "" */

/* The current GL context's GL_RENDERER is the Steam Deck's GPU
   ("VANGOGH" or "Custom GPU 0405" in Mesa's string). Needs a current
   context; 0 without one. renderer (may be NULL) gets the string. */
int device_gl_is_deck(const char **renderer);

/* Pure versions of the two checks, for tests. */
int device_product_kind(const char *product);
int device_renderer_is_deck(const char *renderer);

#endif
