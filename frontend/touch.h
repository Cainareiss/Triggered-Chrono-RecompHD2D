/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On-screen touch controller for phones: a D-pad (8 directions, a finger
 * can slide between them), A/B/X/Y in the SNES diamond, L/R, Start and
 * Select. Multi-touch (SDL_FINGERDOWN/MOTION/UP, tracked by finger id);
 * the output is SNES pad bits (input.h), ORed into pad 1 like the keyboard.
 *
 * Sizes are physical (millimetres, from the display's DPI), shrunk to fit.
 * When the screen is wide the controls sit in the side areas around the
 * picture; otherwise they are anchored to the screen's bottom corners,
 * below the picture when there is room (a phone held upright) and over the
 * picture's bottom corners when there isn't.
 *
 * The overlay hides when a game controller is connected or used, and after
 * a while with no touch; any touch shows it again (and acts). A long press
 * on an empty spot enters edit mode: drag a control to move it, drag the
 * handle at its corner to resize it, long-press an empty spot again to
 * save (touch.ini in SDL's pref dir, ct-recomp/touch) and leave.
 *
 * Drawing is procedural: the overlay is tessellated into triangles
 * (touch_geometry), drawn with SDL_RenderGeometry (touch_draw_sdl) or by
 * the GL drawer in touch_gl.h. */
#ifndef CT_TOUCH_H
#define CT_TOUCH_H

#include <stdint.h>

#include <SDL.h>

typedef struct {
    int x, y, w, h;
} touch_rect;

/* port: look of the controls (settings.ini touch_opacity, touch_size,
   touch_hand; the Controls tab). opacity: percent of the buttons' own,
   size: percent scale of all of them (on top of edit mode's moves), hand: 0
   D-pad on the left, 1 mirrored. Out-of-range values are clamped. Edit
   mode shows the controls fully opaque. */
#define TOUCH_OPACITY_MIN 20
#define TOUCH_OPACITY_DEFAULT 100
#define TOUCH_SIZE_MIN 50
#define TOUCH_SIZE_MAX 150
void touch_options_set(int opacity_pct, int size_pct, int hand);
void touch_options_get(int *opacity_pct, int *size_pct, int *hand);
/* Register the three settings rows (settings.h). */
void touch_register_settings(void);

/* The picture inside a w x h output: the plain path's integer-scaled
   256x224, or a picture of the given aspect (w/h), as large as fits and
   centered (aspect <= 0: the whole output). */
touch_rect touch_picture_plain(int w, int h);
touch_rect touch_picture_aspect(int w, int h, double aspect);

typedef struct {
    int force;          /* --touch: shown from the start; the mouse acts as one finger */
    int hide_seconds;   /* hide after this long with no touch; 0: never */
} touch_opts;

void touch_init(const touch_opts *o);

/* Once per frame, before the events: the output's size in pixels, the
   picture inside it, and the window (for its DPI and for mouse
   coordinates). Lays the controls out. */
void touch_set_screen(SDL_Window *win, int out_w, int out_h, touch_rect picture);

/* Feed every SDL event. Returns 1 if it was a touch (or emulated touch)
   event and is used up; controller events are looked at (they hide the
   overlay) but return 0. frame: the current frame, the clock for the
   long press and the hide timeout. */
int touch_event(const SDL_Event *ev, long frame);

/* Pad bits held by the fingers on the controls; once per frame, after
   the events (frame: the clock for the long press and the hide timeout). */
uint16_t touch_buttons(long frame);
int touch_visible(void);

/* --touch-test FILE: touches injected at given frames, one per line:
 *     FRAME down|move|up TARGET [FINGER]
 * TARGET is a control (up down left right upleft upright downleft
 * downright dpad a b x y l r start select: its centre in the current
 * layout) or X,Y as fractions of the output (0..1). FINGER is a number
 * (default 0). "up" may leave TARGET out: the finger lifts where it is.
 * '#' starts a comment. Returns 0, or -1 with the bad line on stderr. */
int touch_test_load(const char *path);
/* Push the events of frame `frame` (SDL_PushEvent), for the next poll. */
void touch_test_inject(long frame);
/* A test target's point in the current layout, output pixels. Returns 0,
   or -1 for an unknown name. */
int touch_target(const char *name, float *x, float *y);

/* The overlay as triangles in output pixels (top-left origin), for the
   current layout (out_w x out_h = the size given to touch_set_screen) or,
   for a screenshot, laid out again for another size. Returns the vertex
   count (a multiple of 3); the array stays valid until the next call. */
int touch_geometry(const SDL_Vertex **v);
int touch_geometry_for(int out_w, int out_h, touch_rect picture, const SDL_Vertex **v);

/* Draw the overlay with SDL_Renderer over what is there (no logical size
   set: output pixels). */
void touch_draw_sdl(SDL_Renderer *ren);

/* A screenshot of the plain path at w x h: frame (the 256x224 streaming
   texture) integer-scaled and centered, the overlay laid out for w x h on
   top. bgrx gets w*h*4 bytes (png_write_bgrx's layout). Returns 0 or -1. */
int touch_shot_sdl(SDL_Renderer *ren, SDL_Texture *frame, int w, int h, uint8_t *bgrx);

#endif
