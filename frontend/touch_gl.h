/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The touch overlay (touch.h) in OpenGL 3.3 core / ES 3.0, for the HD-2D
 * path: its own tiny shader and vertex buffer, drawn over the renderer's
 * output. Entry points from SDL_GL_GetProcAddress, like hd2d.c. */
#ifndef CT_TOUCH_GL_H
#define CT_TOUCH_GL_H

#include <stdint.h>

#include "touch.h"

/* After the context is current. gles: OpenGL ES 3.0. Returns 0 or -1
   (reason on stderr). */
int touch_gl_init(int gles);
void touch_gl_shutdown(void);

/* Draw the current overlay to the default framebuffer, out_w x out_h. */
void touch_gl_draw(int out_w, int out_h);

/* Draw the overlay, laid out for w x h with the picture at `picture`,
   over a screenshot (bgrx: w*h*4 bytes, rows top first), in place.
   Returns 0 or -1. */
int touch_gl_shot(uint8_t *bgrx, int w, int h, touch_rect picture);

#endif
