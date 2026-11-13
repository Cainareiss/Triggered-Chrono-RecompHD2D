/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The cartridge's battery-backed save RAM (bus_sram(), CT_SRAM_SIZE) on
 * disk, as GAME_ID.srm in the per-game preferences folder
 * (SDL_GetPrefPath("ct-recomp", GAME_ID), the folder the engine's first
 * run uses), or a file given with --save.
 *
 * Load: saves_open, after bus_init and before the first frame. A file of
 * the wrong size is not used or overwritten: it is renamed to .srm.bad
 * (.srm.bad.1, ... if that exists) with a warning.
 *
 * Save: saves_frame at each frame edge compares the SRAM with what is on
 * disk (8 KiB memcmp) and writes it when it differs, at most every
 * SAVES_INTERVAL_MS, and only once it is the same as at the previous frame
 * edge (not halfway through the game's own write) unless it has kept
 * changing for twice that long; saves_open
 * also registers an exit handler that writes any change, so every exit
 * path (the frame limit, Ctrl+Q, SDL_QUIT, a fatal error) saves.
 *
 * Writes are atomic: FILE.tmp is written and fsynced, the previous FILE
 * becomes FILE.bak (one generation), then FILE.tmp is renamed over FILE
 * and the folder fsynced. FILE exists at every point in between. */
#ifndef CT_SAVES_H
#define CT_SAVES_H

#define SAVES_INTERVAL_MS 3000u

/* path: --save FILE, or NULL for the preferences folder. Returns 0, or 1
   when saving is off because the save file can't be used (printed). */
int saves_open(const char *path, const char *game_id);

/* At the edge after frame f: write the SRAM if it changed (see above). */
void saves_frame(long f);

/* Write the SRAM now if it differs from the file. */
void saves_flush(void);

/* Debugging: write the raw SRAM to path at exit (after the last save).
   Works with saving off too. */
void saves_dump_at_exit(const char *path);

/* Testing: spec "F:ADDR=VAL" (decimal F, hex ADDR and VAL) sets SRAM byte
   ADDR to VAL at the edge after frame F, before the change check, as a
   game's save would. Returns 0, or -1 for a bad spec. */
int saves_poke(const char *spec);

#endif
