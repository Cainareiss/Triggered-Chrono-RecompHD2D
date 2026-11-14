/* Input replay scripts: pad 1 buttons per frame, as text.
 *
 * One span per line, "F1-F2:BUTTONS": hold BUTTONS for frames F1..F2
 * (1-based, inclusive). BUTTONS is names joined by '+' (b y select start
 * up down left right a x l r) or a hex mask ("0x1080", bits as in
 * sched_set_joypad). Spans may overlap; their buttons combine. '#' starts
 * a comment. Frame F's buttons are the ones its auto-joypad read sees:
 * the scheduler's frame hook sets them at the edge of frame F-1.
 * "reset F" presses the reset button as frame F begins, at the VBlank
 * whose auto-joypad read would take frame F's buttons (sched.h,
 * sched_set_reset_hook); the game never sees frame F's buttons.
 *
 * port: "p2 F1-F2:BUTTONS" (p1-p4) holds them on that pad instead; a
 * line without the prefix is pad 1, so older scripts read as before.
 *
 * The recorder writes the same format, one span per run of identical
 * nonzero buttons, so a recording replays through replay_load. */
#ifndef CT_REPLAY_H
#define CT_REPLAY_H

#include <stdint.h>
#include <stdio.h>

#define REPLAY_PORTS 4

typedef struct {
    long from, to;
    uint16_t buttons;
    uint8_t port;   /* 0-3: pad 1-4 */
} replay_span;

typedef struct {
    replay_span *spans;
    int n, cap;
    long *resets;   /* frames, in file order */
    int n_resets, cap_resets;
} replay;

/* Add one "F1-F2:BUTTONS" span (pad 1, or the pad a "pN " prefix names);
   0 on success, -1 if malformed. */
int replay_add(replay *r, const char *spec);
/* Add every span in the file at path; 0 on success, -1 if unreadable or
   malformed (the first bad line goes to stderr). */
int replay_load(replay *r, const char *path);
/* Pad 1's buttons at frame; replay_port_buttons: port 0-3's. */
uint16_t replay_buttons(const replay *r, long frame);
uint16_t replay_port_buttons(const replay *r, int port, long frame);
/* Nonzero if any span is for a pad other than pad 1. */
int replay_multi(const replay *r);
/* Nonzero if the script resets as frame `frame` begins. */
int replay_reset(const replay *r, long frame);
void replay_free(replay *r);

typedef struct {
    FILE *f;
    long from[REPLAY_PORTS], last[REPLAY_PORTS];
    uint16_t buttons[REPLAY_PORTS];
} replay_rec;

/* 0 on success, -1 if path can't be written. */
int replay_rec_open(replay_rec *w, const char *path, const char *source);
/* Pad 1's buttons of frame `frame`; call once per frame in order.
   replay_rec_port: the same for port 0-3 (pads after the first are
   written with their "pN " prefix). */
void replay_rec_frame(replay_rec *w, long frame, uint16_t buttons);
void replay_rec_port(replay_rec *w, int port, long frame, uint16_t buttons);
/* Flush the open span and note the frame count; safe to call twice. */
void replay_rec_close(replay_rec *w);

#endif
