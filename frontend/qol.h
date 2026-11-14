/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Quality of life for the SDL frontend (docs/USER-GUIDE.md, "Quality of
 * life"; docs/HANDHELD.md): save states, resume where you left off,
 * fast-forward and turbo, auto-pause, a battery-aware mode, screenshots,
 * always-run, and the plain picture's integer scale and CRT look.
 *
 * main.c calls in at a few places (marked "port: QOL") and lends a few
 * callbacks (qol_host); everything else is here. qol_init hooks the
 * frontend actions (actions.h) it implements: quick save, quick load,
 * fast-forward (hold) and the screenshot, on the keys and back buttons
 * bindings.h gives them (F6, F9, Tab, F8; L4, R4, L5). The other verbs
 * below have keys of their own here, until there are actions for them.
 *
 * Save states (runtime/savestate.h) are taken and loaded only between
 * sched_run_frame calls (qol_after_frame); a request waits for the next
 * frame edge where a state can be taken, usually within a few frames.
 * Files: <data dir>/states/{quick,slot1..slot4,auto}.state, each with a
 * .png thumbnail; <data dir> is SDL's preferences folder for the game (the
 * one holding its .srm), or --states-dir. A state carries the save RAM as
 * it was, but the frontend keeps the cartridge's: after a load the .srm on
 * disk is put back (saves_after_state_load), so loading an old state never
 * rolls back an in-game save.
 *
 * Keys here (never one bindings.h has bound; none of F1-F5, F11, F12):
 *   Shift + the quick save / quick load key: save to / load the selected
 *   slot; F7 select the next slot (1-4); ` (grave) fast-forward on/off;
 *   Print Screen screenshot; Ctrl+T turbo on/off for the buttons held on
 *   pad 1; Ctrl+R the CRT look (plain picture); Caps Lock always run;
 *   Pause or P pause. */
#ifndef CT_QOL_H
#define CT_QOL_H

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

/* What main.c lends: drawing, audio and pacing it owns. */
typedef struct {
    /* What is on screen, full size, as a PNG at path. 0 on success. */
    int (*shot)(const char *path);
    /* Stop playback and empty the sound queue (and the resampler):
       playback resumes once the queue has refilled. */
    void (*audio_restart)(void);
    /* Draw the current frame again (while paused). */
    void (*present)(void);
    /* Window and controller events (while paused). */
    void (*poll)(void);
    /* The frame timer starts over from now (after a pause or a gap). */
    void (*pacing_reset)(void);
    /* settings.ini, after a toggle here. */
    void (*save_settings)(void);
} qol_host;

/* Options and settings, before qol_init. */
int qol_setting(const char *key, const char *val);   /* settings.ini; 1 if it was one */
void qol_settings_write(FILE *f);
int qol_option(int argc, char **argv, int *k);        /* --qol options; 1 if it was one, -1 bad */
void qol_usage(FILE *out);
/* port: the menu's rows for these settings and verbs (settings.h): save
   state slots with their pictures, resume, screenshots (tab "Save
   states"); integer scale, CRT, battery ("Display"); fast-forward, always
   run, turbo, pausing ("Controls"). Their settings.ini lines stay
   qol_setting's; a change saves through the registry's saver. Once, at
   start-up, after main.c's categories. */
void qol_register_settings(void);

/* After the window, audio and the save RAM are set up, before the first
   frame. states: save states on (off with --no-save); replay: a --script
   or --record is running (no resume prompt, no loads while recording). */
void qol_init(const qol_host *h, const char *game_id, int game_mode, int hd2d, int states,
              int recording, int scripted);

/* Every SDL event first (main.c poll_input). 1: it was a QOL key, used. */
int qol_event(const SDL_Event *ev, long frame);
/* At each frame edge, first: whether to show this frame (fast-forward and
   the battery's 30 Hz picture skip some), and the periodic checks. */
int qol_frame(long frame);
/* Live buttons of a port for the next frame: turbo, and buttons held
   through an unpause or the resume prompt kept from the game. */
uint16_t qol_buttons(int port, uint16_t live, long frame);
int qol_audio_muted(void);   /* fast-forward: the game's sound is dropped */
int qol_speed(void);         /* frames per frame time: 1, or the fast-forward speed */
/* The main loop: while paused, qol_idle instead of a frame; after each
   frame, qol_after_frame (save states, screenshots). */
int qol_paused(void);
void qol_idle(void);
void qol_after_frame(void);
/* The loop ended: on a quit (not a fatal error or --frames), the auto
   state for resuming. */
void qol_quit(int quit);

/* The plain (SDL_Renderer) picture: integer scale or fit, the CRT look,
   and the on-screen messages. */
void qol_picture_rect(int w, int h, int *x, int *y, int *pw, int *ph);
void qol_plain_draw(SDL_Renderer *ren, SDL_Texture *frame, int w, int h);
/* Write the plain picture at w x h to path (screenshots). */
int qol_plain_shot(SDL_Renderer *ren, SDL_Texture *frame, int w, int h, const char *path);
/* The HD-2D preset to save in settings.ini: the user's, not the battery's. */
int qol_user_preset(int current);
/* port: the user picked a look (the menu): it beats the battery's from now on. */
void qol_preset_chosen(void);
void qol_osd(const char *msg);

/* Verbs (input actions). Slots: 0 the quick slot, 1-4. */
void qol_quicksave(void);
void qol_quickload(void);
void qol_save_slot(int slot);
void qol_load_slot(int slot);
void qol_slot_next(void);
void qol_ffwd_set(int on);       /* held */
void qol_ffwd_toggle(void);
void qol_turbo_toggle(uint16_t buttons);
void qol_screenshot(void);
void qol_pause_toggle(void);
void qol_always_run_toggle(void);
void qol_crt_toggle(void);

/* Pure helpers, for tests. */
/* /sys/class/power_supply (or dir): 1 on battery, 0 on AC or no battery. */
int qol_on_battery(const char *dir);
/* $XDG_PICTURES_DIR, else user-dirs.dirs' XDG_PICTURES_DIR, else
   ~/Pictures; with "/ct-recomp" appended. */
void qol_pictures_dir(char *out, size_t n);
/* Turbo: is a turbo button pressed on this frame (15 Hz, 2 on 2 off)? */
static inline int qol_turbo_phase(long frame) { return (frame >> 1 & 1) == 0; }

#endif
