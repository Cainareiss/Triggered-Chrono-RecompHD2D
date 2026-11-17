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

#include <time.h>

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

/* port: after a save state's load (frontend/qol.h): the cartridge's save
   RAM is the one on disk, not the state's, so a state never rolls back an
   in-game save. No-op with saving off. */
void saves_after_state_load(void);

/* port: --import-srm / --export-srm: a raw CT_SRAM_SIZE-byte (8 KiB) save
   RAM file, the format SNES emulators share. Import checks the size, keeps
   the save it replaces as FILE.before-import-YYYYmmdd-HHMMSS, and writes
   the new one atomically (as above, with FILE.bak). Export copies the save
   file atomically to `to`. file/game_id find the save as saves_open does.
   0 on success, 1 on failure (printed). */
int saves_import(const char *from, const char *file, const char *game_id);
int saves_export(const char *to, const char *file, const char *game_id);

/* port: rotating backups of the save file, in its folder as
   FILE.backup-YYYYmmdd-HHMMSS (ct.srm.backup-20261116-140322): one is made
   when the game starts and one before each time the save file is replaced
   (unless the newest backup is already the same). Kept: the newest
   BACKUPS_KEEP_LAST, plus the newest of each of the last BACKUPS_KEEP_DAYS
   days that has none among those. */
#define BACKUPS_KEEP_LAST 5
#define BACKUPS_KEEP_DAYS 7
#define BACKUPS_MAX_LIST 64
#define SAVES_RESTORE_ROWS 6   /* the F10 list shows this many */

typedef struct {
    char stamp[16];    /* YYYYmmdd-HHMMSS, as in the file's name */
    char label[24];    /* "2026-11-16 14:03:22" */
    long long when;    /* local time, seconds since 1970 */
} saves_backup;

/* A backup of the save now (after writing out any change). 0 on success
   (also when the newest backup is the same), -1 when saving is off or the
   save can't be read. */
int saves_backup_now(void);
/* The backups, newest first: how many were written to out (at most max). */
int saves_backup_list(saves_backup *out, int max);
/* Make backup `index` (of saves_backup_list) the save: the save it
   replaces is kept as FILE.before-restore-YYYYmmdd-HHMMSS, the running
   game's save RAM becomes the backup's. Refused (-1, nothing touched)
   unless the backup is exactly CT_SRAM_SIZE (8,192) bytes. 0 on success. */
int saves_backup_restore(int index);

/* Testing: the clock the names and pruning use (NULL: time()), and the
   automatic backups on / off (on by default). */
void saves_set_clock(time_t (*fn)(void));
void saves_backup_auto(int on);

/* port: the user's Reset. game_reset_request asks the main loop to press
   the console's reset button at the next frame (as the script's "reset F"
   does); main's reset hook calls game_reset_take, which is 1 once per
   request. */
int game_reset_request(void);
int game_reset_take(void);

/* Register the F10 Save states rows: "Back up now", the restore list,
   "Reset the game" (settings.h). */
void saves_register_settings(void);

#endif
