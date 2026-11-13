/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HD-2D renderer: presents the console's picture as a lit diorama, with
 * depth of field, bloom, actor shadows and a color grade, driven by the
 * PPU's per-pixel layer G-buffer (sched_layers). Everything it draws comes
 * from the running game's own frame; it adds no art of its own.
 *
 * OpenGL 3.3 core or OpenGL ES 3.0; the caller owns the window and makes
 * the context current before hd2d_init. */
#ifndef CT_HD2D_H
#define CT_HD2D_H

#include <stdint.h>

#include "sched.h"

enum { HD2D_OFF, HD2D_SUBTLE, HD2D_FULL, HD2D_PRESETS };
enum { HD2D_DEBUG_NONE, HD2D_DEBUG_CLASSES, HD2D_DEBUG_DEPTH, HD2D_DEBUG_BLUR, HD2D_DEBUG_SPLIT,
       HD2D_DEBUG_BEHIND, HD2D_DEBUGS };
enum { HD2D_ASPECT_4_3, HD2D_ASPECT_PIXEL, HD2D_ASPECT_STRETCH, HD2D_ASPECTS };

/* Load GL entry points (SDL_GL_GetProcAddress), compile the shaders, make
   the fixed-size targets. gles: the context is OpenGL ES 3.0. Returns 0,
   or -1 with the reason printed to stderr. */
int hd2d_init(int gles);
void hd2d_shutdown(void);

void hd2d_set_preset(int preset);
int hd2d_preset(void);
const char *hd2d_preset_name(int preset);
void hd2d_set_debug(int view);
int hd2d_debug(void);
void hd2d_set_aspect(int aspect);
int hd2d_aspect(void);

/* Draw one frame: bgrx and layers are SCHED_WIDTH x SCHED_HEIGHT
   (sched_frame, sched_layers), behind and behind_layers the behind
   G-buffer (sched_behind, sched_behind_layers; NULL if not drawn: the
   layered depth of field and the tilt then fall back to the front layer
   alone), st the PPU state latched with them. Draws to the bound window's
   default framebuffer at out_w x out_h, or with offscreen set to an
   internal target that hd2d_read returns. */
void hd2d_render(const uint8_t *bgrx, const uint8_t *layers, const uint8_t *behind,
                 const uint8_t *behind_layers, const sched_ppu_state *st, int out_w, int out_h,
                 int offscreen);

/* Whether the current preset uses the behind G-buffer, so the caller can
   turn the PPU's (sched_set_behind) on only while it is needed. */
int hd2d_wants_behind(void);

/* After an offscreen hd2d_render of out_w x out_h: copy it as B G R x
   bytes, rows top first (png_write_bgrx's layout). Returns 0 or -1. */
int hd2d_read(uint8_t *bgrx, int out_w, int out_h);

/* Show a short message over the picture for about two seconds (A-Z,
   0-9 and the punctuation in osd_font.h; at most 47 characters). */
void hd2d_osd(const char *text);

/* GPU time of the last hd2d_render in milliseconds (glFinish-bracketed:
   only measured when timing is on, as it stalls the pipeline). */
void hd2d_set_timing(int on);
double hd2d_last_ms(void);

#endif
