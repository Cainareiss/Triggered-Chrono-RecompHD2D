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
   where the game describes its scene; everything else as HD2D_FULL. */
enum { HD2D_OFF, HD2D_SUBTLE, HD2D_FULL, HD2D_DECK, HD2D_3D, HD2D_PRESETS };
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
   0-9 and the punctuation in osd_font.h; at most 47 characters). */
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

/* GPU time of the last hd2d_render in milliseconds (glFinish-bracketed:
   only measured when timing is on, as it stalls the pipeline). */
void hd2d_set_timing(int on);
double hd2d_last_ms(void);

#endif
