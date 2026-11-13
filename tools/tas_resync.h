/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* ct_boot --resync: play a movie's input script against a video of the
 * movie, and shift the inputs where the run falls out of step with it
 * (tools/tas_resync.c). */
#ifndef CT_TAS_RESYNC_H
#define CT_TAS_RESYNC_H

#include <stdint.h>

#include "replay.h"

/* ref: the video as 64x56 8-bit gray frames, 3584 bytes each (ffmpeg -vf
   scale=64:56:flags=area,format=gray -f rawvideo); script frame s is shown
   in video frame s + lag. out: the input script as played (replay.h).
   Forks: the calling process only waits for the run and returns its exit
   status through exit(). Returns 0, or -1 (message printed). */
int resync_init(const char *ref, long lag, const char *out, int jobs);
int resync_active(void);
/* Buttons for frame f (1-based), after any shifts so far; recorded. */
uint16_t resync_buttons(const replay *r, long f);
/* At the edge after frame f: compare its picture (bgrx, SCHED_WIDTH x
   SCHED_HEIGHT) with the video, checkpoint, and resynchronise. */
void resync_frame(long f, const uint8_t *bgrx);
/* A fatal error in frame f: a trial reports it and exits. */
void resync_fatal(long f);
/* The run ends after frame f: write the script, stop the helpers. */
void resync_finish(long f);

#endif
