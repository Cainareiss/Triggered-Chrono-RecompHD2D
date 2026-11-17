/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the Dear ImGui backend both menus share (menu.h, quickmenu.h):
 * one ImGui context over either picture (the HD-2D renderer's OpenGL 3 /
 * ES 3 frame, or the plain picture's SDL_Renderer), the font, the frame
 * begin / end, the glyph pictures (glyphs.h, the Kenney set) as ImGui
 * textures, and the read-back for screenshots. Set up on first use; the
 * host (window, renderer, GL kind) is given once by menu_init.
 * C++ only: the callers draw with ImGui's draw lists. */
#ifndef CT_UI_BACKEND_H
#define CT_UI_BACKEND_H

#include <SDL.h>

#include "imgui.h"

/* Once the window (and renderer or GL context) exists. ren NULL: GL path. */
void ui_set_host(SDL_Window *win, SDL_Renderer *ren, int gles);
int ui_have_host(void);
SDL_Window *ui_window(void);
/* Create the context and backends on first use. 0 when there is no host. */
int ui_ensure(void);
int ui_ready(void);
/* Before the window, renderer or GL context go. */
void ui_shutdown(void);

/* An SDL event for ImGui's own mouse and window handling. */
void ui_process_event(const SDL_Event *ev);

/* One frame: ui_frame_begin (backends' new frame; the display size is w x h
   where SDL doesn't know it), then ImGui::NewFrame and the drawing (the
   caller; fonts: ui_font()), then ui_frame_end (Render and draw over what
   is in the back buffer; the caller presents). */
void ui_frame_begin(int w, int h);
void ui_frame_end(void);

/* The body text size in px for a w x h display (screen size, DPI, the
   "menu_scale" setting). */
float ui_body_px(float w, float h);
/* menu_scale as a factor (0.85, 1.0, 1.2). */
float ui_menu_scale(void);

ImFont *ui_font(void);
void ui_text(ImDrawList *dl, float size, ImVec2 p, ImU32 col, const char *t);
ImVec2 ui_text_size(float size, const char *t);

/* Textures that outlive a frame (previews) and their safe release. */
ImTextureData *ui_make_texture(int w, int h);
void ui_retire_texture(ImTextureData *t);
void ui_collect_retired(void);
/* Free one now (shutdown, the context still there). */
void ui_destroy_texture(ImTextureData *t);

/* A glyph token's picture (in the set in use; its label as a key cap when
   there is none), height hgt, at p, tinted col; returns its width. px is
   the body text size, for the cap. */
float ui_draw_glyph(ImDrawList *dl, const char *token, ImVec2 p, float hgt, float px, ImU32 col);

/* The window as drawn (after the draw, before the present) as a PNG. 0 ok. */
int ui_shot(const char *path, int w, int h);

#endif
