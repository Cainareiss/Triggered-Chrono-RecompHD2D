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

#include "module.h"
#include "sched.h"

/* HD2D_DECK: HD2D_FULL made lighter for the Steam Deck's GPU (device.h).
   HD2D_3D: HD2D_FULL with map views rebuilt as a 3D diorama (scene3d.h)
   where the game describes its scene; everything else as HD2D_FULL.
   HD2D_DECK3D: HD2D_DECK with the 3D diorama, rendered at 67% and
   upscaled (FSR 1), its ambient occlusion at a quarter of the size. */
enum { HD2D_OFF, HD2D_SUBTLE, HD2D_FULL, HD2D_DECK, HD2D_3D, HD2D_DECK3D, HD2D_PRESETS };
enum { HD2D_DEBUG_NONE, HD2D_DEBUG_CLASSES, HD2D_DEBUG_DEPTH, HD2D_DEBUG_BLUR, HD2D_DEBUG_SPLIT,
       HD2D_DEBUG_BEHIND, HD2D_DEBUGS };
enum { HD2D_ASPECT_4_3, HD2D_ASPECT_PIXEL, HD2D_ASPECT_STRETCH, HD2D_ASPECTS };

/* The 3D diorama's quality (HD2D_3D, HD2D_DECK3D; docs/HD2D.md):
   - render scale: the 3D scene is drawn at this share of the picture's
     size, 0.5-1, then upscaled by FSR 1's EASU and sharpened by its RCAS;
     the UI and the 2D path stay at full size. 0: the preset's (1 in
     hd2d3d, 0.67 in deck3d).
   - sharpen, 0-1: CAS at a render scale of 1 (0: off), RCAS's strength
     below it. < 0: the preset's (0 at 1, 0.5 below).
   - ambient occlusion strength, 0-1 (0: off, and the pass is skipped).
     < 0: the preset's.
   Settings (settings.ini render_scale, sharpen, ao); out-of-range values
   are clamped. The getters return what was set (0 / -1: the preset's). */
void hd2d_set_render_scale(float scale);
float hd2d_render_scale(void);
void hd2d_set_sharpen(float amount);
float hd2d_sharpen(void);
void hd2d_set_ao(float strength);
float hd2d_ao(void);

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

/* One throwaway render at out_w x out_h, right after hd2d_init, so the
   driver's first-draw shader builds and the target setup happen before
   the game (and its audio) starts, not on its first frame. */
void hd2d_warmup(int out_w, int out_h);

/* The scene the next hd2d_render shows (runtime/module.h, ct_scene_capture;
   NULL: none). Only read while hd2d_wants_scene() says the preset uses
   it; the pointer must stay valid through that render. */
void hd2d_set_scene(const ct_scene *scene);
int hd2d_wants_scene(void);

/* Whether the current preset uses the behind G-buffer, so the caller can
   turn the PPU's (sched_set_behind) on only while it is needed. */
int hd2d_wants_behind(void);

/* After an offscreen hd2d_render of out_w x out_h: copy it as B G R x
   bytes, rows top first (png_write_bgrx's layout). Returns 0 or -1. */
int hd2d_read(uint8_t *bgrx, int out_w, int out_h);

/* Show a short message over the picture for about two seconds (A-Z,
   0-9 and the punctuation in osd_font.h, and [token] button glyphs:
   osd_text.h, glyphs.h; at most 159 bytes). */
void hd2d_osd(const char *text);

/* port: asset packs (docs/ASSET-PACKS.md). The replacement atlas:
   pages x (size x size) RGBA texels, page after page, and n entries of
   8 ints each: page, x, y, w, h (texels), src_w, src_h (the original
   piece's size in console pixels), 0. NULL / 0 removes it. Copied. */
void hd2d_pack_atlas(const uint8_t *rgba, int size, int pages, const int *rects, int n);
/* The per-pixel replacement map for the frames rendered from now on
   (SCHED_WIDTH x SCHED_HEIGHT x 4 uint16, packs_map_frame's layout), or
   NULL for none. Read during hd2d_render; the caller keeps it alive. */
void hd2d_pack_map(const uint16_t *map);
/* port: asset packs in the 3D diorama (hd2d3d). Asked once a frame for
   each distinct BG tile the 3D meshes show: the 8x8 character at VRAM word
   address chr in the palette whose color 0 is CGRAM index pal, at bpp 2
   or 4, read from the scene's vram and cgram. Returns the atlas entry
   index + 1 that replaces it, 0 for none. NULL: none. Sprites are
   replaced as whole OAM entries (the 2D path); the 3D path draws them as
   8x8 pieces, so it doesn't replace them. */
typedef int (*hd2d_tile_lookup)(void *user, unsigned chr, int pal, int bpp, const uint16_t *vram,
                                const uint16_t *cgram);
void hd2d_pack_tiles(hd2d_tile_lookup fn, void *user);

/* port: an offset for the 3D diorama's camera (HD2D_3D), in degrees:
   dtilt more (> 0) or less tilted back than the preset's, dyaw turned
   about the point it looks at. Clamped to +-HD2D_CAMERA_MAX; 0, 0 (the
   default) is the camera as without it. For gyro aiming (main.c). */
#define HD2D_CAMERA_MAX 10.0f
void hd2d_set_camera_offset(float dtilt, float dyaw);

/* port: the 3D diorama's camera (the menu, settings.ini cam_tilt /
   cam_zoom): tilt in degrees from looking straight down (0-80, default
   40), zoom (0.25-4, default 0.75; 1: the console's 256 pixels across at
   the point it looks at). The offset above (gyro) adds to the tilt.
   CT_HD2D_3D=tilt=..,zoom=.. in the environment still wins at start-up. */
void hd2d_set_camera(float tilt, float zoom);
void hd2d_camera(float *tilt, float *zoom);

/* GPU time of the last hd2d_render in milliseconds (glFinish-bracketed:
   only measured when timing is on, as it stalls the pipeline). */
void hd2d_set_timing(int on);
double hd2d_last_ms(void);

#endif
