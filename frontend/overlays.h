/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the corner overlays: frames per second and frame time, the clock,
 * the battery's charge and the play time. Each is off by default and a
 * settings row (settings.ini overlay_fps, overlay_clock, overlay_battery,
 * overlay_playtime; overlay_corner: tl, tr, bl, br). The data is gathered
 * here (overlays.c, no graphics); overlays_draw (overlays_draw.cpp) paints
 * it through an ImDrawList, so it serves both render paths once the UI
 * backend (ui_backend) is there to give it a list.
 *
 * Play time counts the game's own unpaused frames (60 to the second, so
 * fast-forward counts as game time), kept in playtime.txt in the game's
 * preferences folder, written atomically about once a minute and at exit. */
#ifndef CT_OVERLAYS_H
#define CT_OVERLAYS_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { OVERLAY_FPS, OVERLAY_CLOCK, OVERLAY_BATTERY, OVERLAY_PLAYTIME, OVERLAY_COUNT };
enum { OVERLAY_TL, OVERLAY_TR, OVERLAY_BL, OVERLAY_BR };

/* The preferences folder's play time file for this game (loaded now,
   saved as above). Call once the game is known. */
void overlays_init(const char *game_id);

/* Switch an overlay on or off (also the settings rows' setter). */
void overlays_set(int which, int on);
int overlays_get(int which);
void overlays_set_corner(int corner);
int overlays_get_corner(void);

/* Once per emulated frame, after it: ms is a monotonic clock in
   milliseconds (SDL_GetTicks), emu_frames the count of frames emulated so
   far. Counts play time, polls the battery. */
void overlays_update(unsigned ms, long emu_frames);
/* The time between two frame edges, after the wait (main.c's pacing
   statistics): feeds the frame time overlay. */
void overlays_note_frame(double ms);

/* The text lines of the overlays that are on, in a fixed order (fps,
   clock, battery, play time): each written into lines[k] (n bytes). Returns
   the count (at most max). */
int overlays_lines(char (*lines)[32], int max);

/* The data providers, by themselves (tests). */
double overlays_fps(void);          /* mean of the last frames; 0 before any */
double overlays_frame_ms(void);     /* the same, as a frame time */
double overlays_frame_max_ms(void); /* the slowest of them */
long overlays_playtime_frames(void);
void overlays_playtime_set(long frames);
void overlays_playtime_open(const char *file);   /* load, and save there */
void overlays_playtime_flush(void);              /* write if changed */
void overlays_format_playtime(long frames, char *buf, size_t n);   /* H:MM:SS */
void overlays_format_clock(time_t t, char *buf, size_t n);          /* HH:MM, local */
/* Battery charge 0-100, or -1 when there is none or it can't be read: the
   platform's own backend (Linux /sys/class/power_supply; stubs for
   Windows and Android, see overlays.c). */
int overlays_battery_percent(void);

/* Register the settings rows (settings.h). */
void overlays_register_settings(void);

/* Draw the overlays that are on, over the picture, while no menu is open:
   one UI frame (ui_backend.h) with overlays_draw in it; does nothing, and
   starts no UI frame, while none is on. Call before the buffer swap. */
void overlays_present(int w, int h);

#ifdef __cplusplus
}
struct ImDrawList;
/* Paint the overlays that are on into dl, inside a w x h output. */
void overlays_draw(struct ImDrawList *dl, int w, int h);
#endif

#endif
