/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: on-screen messages as a coverage mask: the 5x7 font
 * (hd2d/osd_font.h) with button glyphs (glyphs.h) where the text has a
 * [token]. Both pictures draw it: HD-2D through its final pass (hd2d.c),
 * the plain one with SDL's renderer (osd_sdl_*).
 *
 * The mask is in units of one font pixel, OSD_OS texels each way: a
 * character takes 6 units (5 and a gap), a glyph OSD_GLYPH_UNITS; the
 * strip is OSD_UNITS_H units high with the text 2 units down from the top
 * and 2 in from the left. */
#ifndef CT_OSD_TEXT_H
#define CT_OSD_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include <SDL.h>

#define OSD_OS 4             /* texels per unit */
#define OSD_UNITS_H 11
#define OSD_GLYPH_UNITS 11   /* GLYPH_PX / OSD_OS */
#define OSD_MAX_TEXELS 2048  /* widest mask (OpenGL ES 3.0's guaranteed texture size) */

/* The mask of text: *mask (malloc'd, the caller frees) is *w x *h bytes,
   *w = units * OSD_OS, *h = OSD_UNITS_H * OSD_OS. Returns 0, or -1 when
   out of memory. A message too wide is cut off at OSD_MAX_TEXELS. */
int osd_text_mask(const char *text, uint8_t **mask, int *w, int *h);

/* The text with each [token] as its label ("Press Menu"), for stderr and
   logs. */
void osd_text_plain(const char *text, char *out, size_t n);

/* The plain picture's message, drawn as hd2d_osd's: about two seconds,
   the last 20 frames fading, over a darkened strip. */
void osd_sdl_show(const char *text);
/* Draw it at (x, y) with each unit scale pixels; tick: a frame passed
   (screenshots don't use the time up). */
void osd_sdl_draw(SDL_Renderer *ren, int x, int y, int scale, int tick);
/* Free the texture (before the renderer goes). */
void osd_sdl_shutdown(void);

#endif
